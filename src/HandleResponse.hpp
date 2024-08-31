#pragma once

#include "Parser.hpp"
#include <algorithm>
#include <asm-generic/errno.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

// TODO: Study how to make this recursive instead of iterative

class HandleResponse {

public:
  HandleResponse(RespData result, int client_fd);

private:
  const char *ping_response = "+PONG\r\n";
  std::string echo_response;
  int m_client_fd;

  void empty();
  void array(RespData result);
  void ping();
  void echo(std::vector<RespData>::const_iterator command,
            const std::vector<RespData> &command_array);
};
