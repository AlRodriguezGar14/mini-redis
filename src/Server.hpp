#pragma once

#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class Server {
private:
  int m_server_fd;
  struct sockaddr_in m_server_addr;
  int m_connection_backlog;

public:
  Server() : m_connection_backlog(5) {
    struct sockaddr_in server_addr {
      .sin_family = AF_INET, .sin_port = htons(6379), .sin_addr = INADDR_ANY
    };
    m_server_addr = server_addr;
  };
  int init_server() {
    m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(m_server_fd, (struct sockaddr *)&m_server_addr,
             sizeof(m_server_addr)) != 0) {
      std::cerr << "Could not bind server\n";
      return -1;
    }
    return 0;
  }

  int listen_connection() {
    if (listen(m_server_fd, m_connection_backlog) != 0) {
      std::cerr << "Could not listen for the client" << std::endl;
      return -1;
    }
    return 0;
  }

  int fd() { return m_server_fd; };

  void close_server() { close(m_server_fd); }
};
