#include "Server.hpp"
#include "HandleResponse.hpp"
#include "Parser.hpp"
#include <algorithm>
#include <asm-generic/errno.h>
#include <cstdint>
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
    if (read_persistence() == -1)
      return -1;
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

  if (read_persistence() == -1)
    return -1;
  return 0;
}

std::string Server::parse_value(const std::string &needle,
                                const std::string &haystack,
                                const std::string &separator) {
  int start = haystack.find(needle) + strlen(needle.c_str());
  int end = haystack.find(separator) - start;

  return haystack.substr(start, end);
}

/*
Length encoding is used to store the length of the next object in the stream.
Length encoding is a variable byte encoding designed to use as few bytes as
possible.

This is how length encoding works : Read one byte from the stream, compare the
two most significant bits:

Bits  How to parse
00    The next 6 bits represent the length
01    Read one additional byte. The combined 14 bits represent the length
10    Discard the remaining 6 bits. The next 4 bytes from the stream represent
      the length
11    The next object is encoded in a special format. The remaining 6
      bits indicate the format. May be used to store numbers or Strings, see
      String Encoding

As a result of this encoding:
Numbers up to and including 63 can be stored in 1 byte
Numbers up to and including 16383 can be stored in 2 bytes
Numbers up to 2^32 -1 can be stored in 4 bytes
*/
uint64_t get_str_bytes_len(std::ifstream &rdb) {
  uint8_t first_byte;
  //!!!!!! without reinterpret_cast<char*> the read() function won't work
  rdb.read(reinterpret_cast<char *>(&first_byte), 1);

  // 0xCO = 11000000
  // if first_byte & 0xCO == 0, means that the MSBs of first_byte is 0
  if ((first_byte & 0xC0) == 0) {
    // 0x3F = 00111111
    // first_byte & 0x3F returns the lower 6-bits
    return first_byte & 0x3F;
  }

  // 0x40 = 01000000
  if ((first_byte & 0xC0) == 0x40) {
    uint8_t next_byte; // this holds the next 8-bits
    rdb.read(reinterpret_cast<char *>(&next_byte), 1);
    // Combine the previous 6 with the next 8; 14-bits in total
    uint64_t length = ((first_byte & 0x3F) << 8) | next_byte;
    return length;
  }
  // 0x80 = 10000000
  if ((first_byte & 0xC0) == 0x80) {
    // Instead of reading byte a byte like before, now C++ has tools
    uint32_t length;
    rdb.read(reinterpret_cast<char *>(&length), sizeof(length));
    length = be32toh(length); // Convert from big-endian to host
    return length;
  }
  if (first_byte == 0xFB) {
    // This is a special case for database sizes
    return first_byte;
  }

  std::cout << "Special encoding or error in length byte" << std::endl;
  return 0;
}

/* About string encoding:
  https://rdb.fnordig.de/file_format.html#length-encoding

  There are three types of Strings in Redis:

  > Length prefixed strings
  > An 8, 16 or 32 bit integer - pending to implement
  > A LZF compressed string - pending to implement

 Length prefixed strings are quite simple. The length of the string in bytes is
 first encoded using Length Encoding. After this, the raw bytes of the string
 are stored.
 */

std::string read_byte_to_string(std::ifstream &rdb) {

  uint64_t len = get_str_bytes_len(rdb);

  std::vector<char> buffer(len);
  rdb.read(buffer.data(), len);

  return std::string(buffer.data(), len);
}

// encoding -> https://rdb.fnordig.de/file_format.html#length-encoding
// https://github.com/sripathikrishnan/redis-rdb-tools/wiki/Redis-RDB-Dump-File-Format
// https://app.codecrafters.io/courses/redis/stages/jz6
// https://rdb.fnordig.de/file_format.html
/*
At a high level, the RDB file has the following structure

----------------------------#
52 45 44 49 53              # Magic String "REDIS"
30 30 30 33                 # RDB Version Number as ASCII string. "0003" = 3
----------------------------
FA                          # Auxiliary field
$string-encoded-key         # May contain arbitrary metadata
$string-encoded-value       # such as Redis version, creation time, used memory,
...
----------------------------
FE 00                       # Indicates database selector. db number = 00
FB                          # Indicates a resizedb field
$length-encoded-int         # Size of the corresponding hash table
$length-encoded-int         # Size of the corresponding expire hash table
----------------------------# Key-Value pair starts
FD $unsigned-int            # "expiry time in seconds", followed by 4 byte
unsigned int $value-type                 # 1 byte flag indicating the type of
value $string-encoded-key         # The key, encoded as a redis string
$encoded-value              # The value, encoding depends on $value-type
----------------------------
FC $unsigned long           # "expiry time in ms", followed by 8 byte unsigned
long $value-type                 # 1 byte flag indicating the type of value
$string-encoded-key         # The key, encoded as a redis string
$encoded-value              # The value, encoding depends on $value-type
----------------------------
$value-type                 # key-value pair without expiry
$string-encoded-key
$encoded-value
----------------------------
FE $length-encoding         # Previous db ends, next db starts.
----------------------------
...                         # Additional key-value pairs, databases, ...

FF                          ## End of RDB file indicator
8-byte-checksum             ## CRC64 checksum of the entire file.
 */
int Server::read_persistence() {
  config.file = config.dir + "/" + config.db_filename;
  std::ifstream rdb(config.file, std::ios::binary);
  if (!rdb.is_open()) {
    std::cerr << "Could not open the Redis Persistent Database: " << config.file
              << std::endl;
    return -1;
  }

  char header[9];
  rdb.read(header, 9);
  std::cout << "Header: " << std::string(header, 9) << std::endl;

  // metadata
  while (true) {
    char opcode;
    if (!rdb.read(&opcode, 1)) {
      std::cout << "Reached end of file while looking for database start"
                << std::endl;
      return 0;
    }

    if (opcode == 0xFA) {
      std::string key = read_byte_to_string(rdb);
      std::string value = read_byte_to_string(rdb);
      std::cout << key << " " << value << std::endl;
    }
    if (opcode == 0xFE) {
      uint64_t db_number = get_str_bytes_len(rdb);
      std::cout << "Database number: " << db_number << std::endl;
    }
    if (opcode == 0xFB) {
      uint64_t hash_table_size = get_str_bytes_len(rdb);
      uint64_t expire_hash_table_size = get_str_bytes_len(rdb);
      std::cout << "Hash table size: " << hash_table_size
                << ", Expire hash table size: " << expire_hash_table_size
                << std::endl;
      break;
    }
  }

  // Read key-value pairs
  while (true) {
    char opcode;
    if (!rdb.read(&opcode, 1)) {
      std::cout << "Reached end of file" << std::endl;
      break;
    }
    if (opcode == 0xFF) {
      std::cout << "Reached end of database marker" << std::endl;
      uint64_t checksum;
      rdb.read(reinterpret_cast<char *>(&checksum), sizeof(checksum));
      checksum = be64toh(checksum);
      std::cout << "db checksum: " << checksum << std::endl;
      break;
    }

    uint64_t expire_time = 0;
    uint64_t expire_date = 0;
    if (opcode ==
        0xFD) { // expiry time in seconds followed by 4 byte - uint32_t
      uint32_t seconds;
      rdb.read(reinterpret_cast<char *>(&seconds), sizeof(seconds));
      expire_time = be32toh(seconds);
      rdb.read(&opcode, 1);
    }
    if (opcode == 0xFC) { // expiry time in ms, followd by 8 byte
                          // unsigned - uint64_t
      rdb.read(reinterpret_cast<char *>(&expire_time), sizeof(expire_time));
      expire_date = be64toh(expire_time);
      rdb.read(&opcode, 1);
    }

    // After 0xFD and 0x FC, comes the key-pair-value
    std::string key = read_byte_to_string(rdb);
    std::string value = read_byte_to_string(rdb);

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    if (expire_time == 0 || expire_time > now) {
      std::cout << "adding " << key << " to the in memory database"
                << std::endl;
      config.db.insert_or_assign(key, DB_Entry({value, 0, expire_time}));
    }
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
