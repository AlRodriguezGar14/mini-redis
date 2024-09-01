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

typedef std::map<std::string, std::string> database;
typedef std::map<std::string, uint64_t> expire_database;

// Redis request parser
struct Request {
  std::string command;
  std::vector<std::string> args;
};

class Server {
private:
  int m_server_fd;
  int m_connection_backlog;
  int m_port;
  database db;
  expire_database expire_db;

  bool handle_client(int client_fd);
  void set_nonblocking(int sock);
  int parse_request(Request &req, const std::string &buffer);

public:
  Server(int port);
  int init_server();
  void listen_connections();

  int fd() { return m_server_fd; };
  void close_server() { close(m_server_fd); }
};
