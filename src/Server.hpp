#pragma once

#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

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
  // std::unordered_set<int> m_clients;

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
