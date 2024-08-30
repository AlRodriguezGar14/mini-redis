#pragma once

// #include <iostream>
// #include <netinet/in.h>
// #include <sys/socket.h>
// #include <unistd.h>
#include "Client.hpp"
#include "Server.hpp"
#include <cstring>
#include <strings.h>

#define BUFF_SIZE 4096

class Connection {
private:
  Client &m_client;
  Server &m_server;

public:
  Connection(Client &t_client, Server &t_server)
      : m_client(t_client), m_server(t_server){};

  void response(std::string message, std::string status_code) {
    if (m_client.fd() < 0)
      return;
    std::string content_length_header =
        "Content-Length: " + std::to_string(message.length()) + "\r\n";
    send(m_client.fd(), status_code.c_str(), status_code.length(), 0);
    send(m_client.fd(), "Content-Type: text/plain\r\n", 26, 0);
    send(m_client.fd(), content_length_header.c_str(),
         content_length_header.length(), 0);

    send(m_client.fd(), "\r\n", 2, 0);
    send(m_client.fd(), message.c_str(), message.length(), 0);
  }

  void listen() {
    char buffer[BUFF_SIZE] = "";
    memset(buffer, '\0', BUFF_SIZE);
    int bytes_received;

    while ((bytes_received = recv(m_client.fd(), &buffer, 4096, 0)) > 0) {
      std::cout << buffer << std::endl;
      if (std::string(buffer) == "*1\r\n$4\r\nPING\r\n") {
        ping();
      }
      bzero(buffer, bytes_received);
    }
  }

  void ping() { send(m_client.fd(), "+PONG\r\n", 7, 0); }
};
