#pragma once

#include <cstdint>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Parser.hpp"

// Redis request parser
struct Request {
  std::string command;
  std::vector<std::string> args;
};

struct DB_Entry {
  std::string value;
  uint64_t date;
  uint64_t expiry;
};

typedef std::map<std::string, DB_Entry> database;

struct DB_Config {
  std::string dir;
  std::string db_filename;
  std::string file;
  database db;
  database in_memory_db;
};

class Server {
private:
  int m_server_fd;
  int m_connection_backlog;
  int m_port;
  DB_Config config;

  bool handle_client(int client_fd);
  void set_nonblocking(int sock);
  int parse_request(Request &req, const std::string &buffer);
  int set_persistence(int argc, char **argv);
  int read_persistence();
  int write_persistence();
  void how_to_use();

public:
  Server(int port, int argc = 0, char **argv = NULL);
  int init_server();
  void listen_connections();

  int fd() { return m_server_fd; };
  void close_server() { close(m_server_fd); }

  std::string parse_value(const std::string &needle,
                          const std::string &haystack,
                          const std::string &separator);
};
