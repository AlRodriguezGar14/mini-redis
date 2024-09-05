#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "Server.hpp"

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  Server server(argc, argv);
  server.listen_connections();
  server.close_server();

  return 0;
}
