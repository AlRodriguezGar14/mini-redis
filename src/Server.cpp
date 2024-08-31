#include "Server.hpp"
#include <asm-generic/errno.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTS 100

void Server::set_nonblocking(int sock) {
  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

Server::Server(int port) : m_connection_backlog(5), m_port(port) {
  if (init_server() < 0)
    exit(1);
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
  }
  close(epoll_fd);
}

bool Server::handle_client(int client_fd) {
  char buffer[1024] = {0};
  const char *response = "+PONG\r\n";
  ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (bytes_read <= 0) {
    if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      return false;
    }
  } else {
    buffer[bytes_read] = '\0'; // Null-terminate the received data
    if (std::string(buffer) == "*1\r\n$4\r\nPING\r\n") {
      std::cout << "request:\n" << buffer << std::endl;
      send(client_fd, response, strlen(response), 0);
    }
  }
  return true;
}
