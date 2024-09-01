#pragma once

#include "Parser.hpp"
#include "Server.hpp"
#include <algorithm>
#include <asm-generic/errno.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

class HandleResponse {

public:
  HandleResponse(RespData result, int client_fd, DB_Config &config);

private:
  const char *ping_response = "+PONG\r\n";
  std::string echo_response;
  int m_client_fd;

  void ok();
  void null();
  void empty();
  void array(RespData result, DB_Config &config);
  void ping();
  void echo(size_t &i, const std::vector<RespData> &command_array);
  void set(size_t &i, const std::vector<RespData> &command_array,
           DB_Config &config);
  void get(size_t &i, const std::vector<RespData> &command_array,
           DB_Config &config);

  int check_expire_ms(std::string key, DB_Config &config);
};
