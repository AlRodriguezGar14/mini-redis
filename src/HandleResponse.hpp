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

// TODO: Study how to make this recursive instead of iterative

class HandleResponse {

public:
  HandleResponse(RespData result, int client_fd, database &db,
                 expire_database &expire_db);

private:
  const char *ping_response = "+PONG\r\n";
  std::string echo_response;
  int m_client_fd;

  void ok();
  void null();
  void empty();
  void array(RespData result, database &db, expire_database &expire_db);
  void ping();
  void echo(size_t &i, const std::vector<RespData> &command_array);
  void set(size_t &i, const std::vector<RespData> &command_array, database &db,
           expire_database &expire_db);
  void get(size_t &i, const std::vector<RespData> &command_array, database &db,
           expire_database &expire_db);

  void set_expire_ms(std::string key, std::string timecode_str,
                     expire_database &expire_db);

  int check_expire_ms(std::string key, database &db,
                      expire_database &expire_db);
};
