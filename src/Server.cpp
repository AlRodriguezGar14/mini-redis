#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Client.hpp"
#include "Connection.hpp"
#include "Server.hpp"

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  Server server;
  Client client;

  std::cout << "Server started\n";

  if (server.init_server() < 0)
    return 1;
  if (server.listen_connection() < 0)
    return 1;

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server.fd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) <
      0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  std::cout << "Waiting for a client to connect...\n";

  if (client.accept_connection(server.fd()) < 0) {
    server.close_server();
    return 1;
  }
  Connection connection(client, server);
  connection.ping();

  server.close_server();

  return 0;
}
