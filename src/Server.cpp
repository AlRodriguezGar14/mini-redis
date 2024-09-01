#include "Server.hpp"
#include "HandleResponse.hpp"
#include <algorithm>
#include <asm-generic/errno.h>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTS 100

Server::Server(int port, int argc, char **argv)
    : m_connection_backlog(5), m_port(port) {
  if (set_persistence(argc, argv) == -1)
    exit(1);
  if (init_server() < 0)
    exit(1);
}

void Server::how_to_use() {
  std::cout << "\n\tIncorrect input. How to use:\n\t"
            << "./your_program.sh --dir /tmp/redis-files --dbfilename dump.rdb"
            << std::endl;
}

int Server::set_persistence(int argc, char **argv) {
  if (argc != 5 && argc != 1)
    return how_to_use(), -1;

  if (argc == 1) {
    std::cout << "Setting default values\n";
    config.dir = ".";
    config.db_filename = "default.rdb";
    // if (read_persistence() == -1)
    //   return -1;
    return 0;
  }

  if (strncmp(argv[1], "--dir", strlen(argv[1])) == 0)
    config.dir = std::string(argv[2]);
  else {
    return how_to_use(), -1;
  }
  if (strncmp(argv[3], "--dbfilename", strlen(argv[3])) == 0)
    config.db_filename = std::string(argv[4]);
  else {
    return how_to_use(), -1;
  }
  if (config.db_filename.substr(config.db_filename.length() - 4) != ".rdb")
    return how_to_use(), -1;

  // if (read_persistence() == -1)
  //   return -1;
  return 0;
}

std::string Server::parse_value(const std::string &needle,
                                const std::string &haystack,
                                const std::string &separator) {
  int start = haystack.find(needle) + strlen(needle.c_str());
  int end = haystack.find(separator) - start;

  return haystack.substr(start, end);
}

int Server::read_persistence() {

  config.file = config.dir + "/" + config.db_filename;

  std::ifstream rdb(config.file);
  if (!rdb.is_open()) {
    std::cerr << "Could not open the Redis Persistent Database" << std::endl;
    return -1;
  }
  std::string line;
  while (getline(rdb, line)) {

    std::string key = parse_value("[key]", line, "[/key]");
    std::string value = parse_value("[value]", line, "[/value]");
    std::string date = parse_value("[date]", line, "[/date]");
    std::string expiry = parse_value("[expiry]", line, "[/expiry]");

    DB_Entry entry = {value, std::strtoull(date.c_str(), NULL, 10), 0};
    if (expiry != "0") {
      entry.expiry = strtoull(expiry.c_str(), NULL, 10);
    }
    config.db.insert_or_assign(key, entry);
  }
  rdb.close();
  return 0;
}

int Server::write_persistence() {
  std::ofstream rdb(config.file, std::ios::app);
  if (!rdb.is_open()) {
    std::cerr << "Could not save to the Redis Persistent Database" << std::endl;
    return -1;
  }
  uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
  for (auto entry : config.in_memory_db) {
    if (entry.second.expiry != 0 && entry.second.expiry < now) {
      try {
        config.db.at(entry.first);
        config.db.erase(entry.first);
      } catch (const std::exception &e) {
      }
      continue;
    }
    rdb << "[key]" << entry.first << "[/key]"
        << "[value]" << entry.second.value << "[/value]"
        << "[date]" << entry.second.date << "[/date]"
        << "[expiry]" << entry.second.expiry << "[/expiry]\n";
  }
  rdb.close();
  config.in_memory_db.clear();

  return 0;
}

void Server::set_nonblocking(int sock) {
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

int Server::init_server() {
  m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_server_fd < 0) {
    std::cerr << "Could not create socket\n";
    return -1;
  }
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(m_port);
  server_addr.sin_addr.s_addr = INADDR_ANY;

  int reuse = 1;
  if (setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    close(m_server_fd);
    return -1;
  }

  if (bind(m_server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) !=
      0) {
    std::cerr << "Could not bind server\n";
    return -1;
  }

  if (listen(m_server_fd, m_connection_backlog) != 0) {
    std::cerr << "Could not listen for the client" << std::endl;
    return -1;
  }

  set_nonblocking(m_server_fd);
  return 0;
}

void Server::listen_connections() {
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    std::cerr << "Failed to create epoll file descriptor" << std::endl;
    close_server();
    exit(1);
  }

  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = m_server_fd;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, m_server_fd, &event)) {
    std::cerr << "Failed to add fd to epoll" << std::endl;
    close(epoll_fd);
    close_server();
    exit(1);
  }

  struct epoll_event events[MAX_EVENTS];
  while (true) {
    int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    for (int i = 0; i < event_count; ++i) {
      if (events[i].data.fd == m_server_fd) {
        // New connection
        int client_fd = accept(m_server_fd, NULL, NULL);
        if (client_fd < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK)
            std::cerr << "Accept error\n";
          continue;
        }
        set_nonblocking(client_fd);
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = client_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
          std::cerr << "Failed to add client to epoll" << std::endl;
          close(client_fd);
        }
      } else {
        // Active client
        if (!handle_client(events[i].data.fd)) {
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
          close(events[i].data.fd);
        }
      }
    }
    /* Each connection is writting to the file persistence.
     * TODO: Set a better trigger
     * */
    // if (config.in_memory_db.size() > 0)
    //   write_persistence();
  }
  close(epoll_fd);
}

bool Server::handle_client(int client_fd) {
  Request req;
  char buffer[1024] = {0};

  ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (bytes_read <= 0) {
    if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      return false;
    }
  } else {
    buffer[bytes_read] = '\0'; // Null-terminate the received data
    std::cout << "\nRequest:\n" << buffer;

    RespParser parser;

    try {
      RespData result = parser.parse(std::string(buffer));
      parser.printRespData(result);
      HandleResponse respond(result, client_fd, config);

    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << std::endl;
    }
  }
  return true;
}

void print_request(const Request &req) {
  std::cout << "Parsed request:\n";
  std::cout << "\tCommand: " << req.command << std::endl;
  std::cout << "\tArguments: ";
  for (const auto &arg : req.args)
    std::cout << " " << arg;
  std::cout << std::endl;
}
int Server::parse_request(Request &req, const std::string &buffer) {

  std::string remaining_buffer = buffer;

  if (remaining_buffer[0] != '*') {
    std::cerr << "Invalid request\n";
    return -1;
  }

  size_t pos = remaining_buffer.find("\r\n");
  int numb_args = std::stoi(remaining_buffer.substr(1, pos - 1));
  remaining_buffer = remaining_buffer.substr(pos + 2);

  for (int i = 0; i < numb_args; ++i) {
    if (remaining_buffer[0] != '$') {
      std::cerr << "Invalid request\n";
      return -1;
    }
    pos = remaining_buffer.find("\r\n");
    int arg_size = std::stoi(remaining_buffer.substr(1, pos - 1));
    remaining_buffer = remaining_buffer.substr(pos + 2);

    std::string arg = remaining_buffer.substr(0, arg_size);
    remaining_buffer = remaining_buffer.substr(arg_size + 2);
    if (i == 0) {
      std::transform(arg.begin(), arg.end(), arg.begin(), ::toupper);
      req.command = arg;
    } else
      req.args.push_back(arg);
  }
  print_request(req);
  return 0;
}
