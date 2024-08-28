#pragma once

#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class Client {

private:
  int m_client_fd;
  struct sockaddr_in m_client_addr;
  unsigned int m_client_addr_len;

public:
  int accept_connection(int t_server_fd) {
    m_client_addr_len = sizeof(m_client_addr);
    m_client_fd = accept(t_server_fd, (struct sockaddr *)&m_client_addr,
                         (socklen_t *)&m_client_addr_len);
    if (m_client_fd < 0) {
      std::cerr << "Failed to accept connection\n" << std::endl;
      return -1;
    }
    std::cout << "Client connected\n" << std::endl;
    return 0;
  }
  int fd() { return m_client_fd; };
};
