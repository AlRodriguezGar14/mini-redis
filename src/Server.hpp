#pragma once

#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_set>

class Server {
private:
  int m_server_fd;
  int m_connection_backlog;
  int m_port;
  std::unordered_set<int> m_clients;

  bool handle_client(int client_fd);

public:
  Server(int port);
  int init_server();
  void listen_connections();

  int fd() { return m_server_fd; };
  void close_server() { close(m_server_fd); }
};
