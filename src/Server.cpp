#include "Server.hpp"
#include <asm-generic/errno.h>
#include <fcntl.h>

bool Server::handle_client(int client_fd) {
  char buffer[1024] = {0};
  const char *response = "+PONG\r\n";

  if (recv(client_fd, &buffer, 1024, 0) < 0)
    return false;

  if (std::string(buffer) == "*1\r\n$4\r\nPING\r\n") {
    std::cout << "request:\n" << buffer << std::endl;
    send(client_fd, response, strlen(response), 0);
  }
  return true;
}

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

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    close(m_server_fd);
    return 1;
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
  return 0;

  set_nonblocking(m_server_fd);
}

void Server::listen_connections() {
  while (42) {
    fd_set read_fds;

    FD_ZERO(&read_fds);
    FD_SET(m_server_fd, &read_fds);

    int max_fd = m_server_fd; // keep track of the highest fd - needed for
                              // setting a range for select()
    for (int client_fd : m_clients) {
      FD_SET(client_fd, &read_fds);
      max_fd = std::max(max_fd, client_fd);
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (activity < 0) {
      perror("select error");
      exit(1);
    }
    // FD_ISSET when select() considers the fd has activity
    if (FD_ISSET(m_server_fd, &read_fds)) {
      int new_socket = accept(m_server_fd, NULL, NULL);
      if (new_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          perror("accept went wrong");
          exit(1);
        }
      } else {
        set_nonblocking(new_socket);
        m_clients.insert(new_socket);
      }
    }
    for (auto it = m_clients.begin(); it != m_clients.end();) {
      int client_fd = *it;
      if (FD_ISSET(client_fd, &read_fds)) {
        if (!handle_client(client_fd)) {
          close(client_fd);
          it = m_clients.erase(it);
        } else {
          ++it;
        }
      } else {
        ++it;
      }
    }
  }
}
