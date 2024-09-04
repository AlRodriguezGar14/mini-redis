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

Bits  How to parse - read the two most important bits first
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
#include <cassert>
#include <optional>

template <typename T = char> T read(std::ifstream &rdb) {
  T val;
  rdb.read(reinterpret_cast<char *>(&val), sizeof(val));
  return val;
}

std::pair<std::optional<uint64_t>, std::optional<int8_t>>
get_str_bytes_len(std::ifstream &rdb) {
  // Read the first byte from the RDB file
  auto byte = read<uint8_t>(rdb);

  // Get the two most significant bits of the byte
  // These bits determine how the length is encoded
  auto sig = byte >> 6; // 0 bytes, 1, 2, 3 - 00, 01, 10, 11

  switch (sig) {
  case 0: {
    // If the two most significant bits are 00
    // The length is the lower 6 bits of the byte
    return {byte & 0x3F, std::nullopt};
  }
  case 1: {
    // If the two most significant bits are 01
    // The length is the lower 6 bits of the first byte and the whole next byte
    auto next_byte = read<uint8_t>(rdb);
    uint64_t sz = ((byte & 0x3F) << 8) | next_byte;
    return {sz, std::nullopt};
  }
  case 2: {
    // If the two most significant bits are 10
    // The length is the next 4 bytes
    uint64_t sz = 0;
    for (int i = 0; i < 4; i++) {
      auto byte = read<uint8_t>(rdb);
      sz = (sz << 8) | byte;
    }
    return {sz, std::nullopt};
  }
  case 3: {
    // If the two most significant bits are 11
    // The string is encoded as an integer
    switch (byte) {
    case 0xC0:
      // The string is encoded as an 8-bit integer of 1 byte
      return {std::nullopt, 8};
    case 0xC1:
      // The string is encoded as a 16-bit integer of 2 bytes
      return {std::nullopt, 16};
    case 0xC2:
      // The string is encoded as a 32-bit integer of 4 bytes
      return {std::nullopt, 32};
    case 0xFD:
      // Special case for database sizes
      return {byte, std::nullopt};
    default:
      return {std::nullopt, 0};
    }
  }
  }
  return {std::nullopt, 0};
}

std::string read_byte_to_string(std::ifstream &rdb) {
  std::pair<std::optional<uint64_t>, std::optional<int8_t>> decoded_size =
      get_str_bytes_len(rdb);

  if (decoded_size.first.has_value()) { // the length of the string is prefixed
    int size = decoded_size.first.value();
    std::vector<char> buffer(size);
    rdb.read(buffer.data(), size);
    return std::string(buffer.data(), size);
  }
  assert(
      decoded_size.second.has_value()); // the string is encoded as an integer
  int type = decoded_size.second.value();
  switch (type) {
  case 8: {
    auto val = read<int8_t>(rdb);
    return std::to_string(val);
  }
  case 16: { // 16 bit integer, 2 bytes
    auto val = read<int16_t>(rdb);
    return std::to_string(val);
  }
  case 32: { // 32 bit integer, 4 bytes
    auto val = read<int32_t>(rdb);
    return std::to_string(val);
  }
  default:
    return "";
  }
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
$string-encoded-value       # such as Redis version, creation time, used
memory,
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
      std::cout << "AUX: " << key << " " << value << std::endl;
    }
    if (opcode == 0xFE) {
      auto db_number = get_str_bytes_len(rdb);
      if (db_number.first.has_value()) {
        std::cout << "SELECTDB: Database number: " << db_number.first.value()
                  << std::endl;
        opcode =
            read<char>(rdb); // Read the next opcode after the database number
      }
    }
    if (opcode == 0xFB) {
      auto hash_table_size = get_str_bytes_len(rdb);
      auto expire_hash_table_size = get_str_bytes_len(rdb);
      if (hash_table_size.first.has_value() &&
          expire_hash_table_size.first.has_value()) {
        std::cout << "RESIZEDB: Hash table size: "
                  << hash_table_size.first.value()
                  << ", Expire hash table size: "
                  << expire_hash_table_size.first.value() << std::endl;
      }
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
      std::cout << "EOF: Reached end of database marker" << std::endl;
      uint64_t checksum;
      rdb.read(reinterpret_cast<char *>(&checksum), sizeof(checksum));
      checksum = be64toh(checksum);
      std::cout << "db checksum: " << checksum << std::endl;
      break;
    }

    uint64_t expire_time_s = 0;
    uint64_t expire_time_ms = 0;
    if (opcode ==
        0xFD) { // expiry time in seconds followed by 4 byte - uint32_t
      uint32_t seconds;
      rdb.read(reinterpret_cast<char *>(&seconds), sizeof(seconds));
      expire_time_s = be32toh(seconds);
      rdb.read(&opcode, 1);
      std::cout << "EXPIRETIME: " << expire_time_ms << std::endl;
    }
    if (opcode == 0xFC) { // expiry time in ms, followd by 8 byte
                          // unsigned - uint64_t
      rdb.read(reinterpret_cast<char *>(&expire_time_ms),
               sizeof(expire_time_ms));
      expire_time_ms = be64toh(expire_time_ms);
      rdb.read(&opcode, 1);
      std::cout << "EXPIRETIMEMS: " << expire_time_ms << std::endl;
    }

    // After 0xFD and 0x FC, comes the key-pair-value
    std::string key = read_byte_to_string(rdb);
    std::string value = read_byte_to_string(rdb);

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    if (expire_time_s == 0 || expire_time_ms > now) {
      std::cout << "adding " << key << " - " << value << std::endl;
      config.db.insert_or_assign(key, DB_Entry({value, 0, expire_time_s}));
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
