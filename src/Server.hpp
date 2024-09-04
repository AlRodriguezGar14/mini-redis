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

#include "DB.hpp"
#include "Parser.hpp"
#include "RDB_Decoder.hpp"

class Server {
private:
  int m_server_fd;
  int m_connection_backlog;
  int m_port;
  DB_Config config;

  bool handle_client(int client_fd);
  void set_nonblocking(int sock);
  int parse_request(Request &req, const std::string &buffer);
  int set_rdb(int argc, char **argv);
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
