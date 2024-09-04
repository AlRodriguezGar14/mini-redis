#pragma once

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>

#include "DB.hpp"

class RDB_Decoder {
private:
  DB_Config config;
  std::pair<std::optional<uint64_t>, std::optional<int8_t>>
  get_str_bytes_len(std::ifstream &rdb);
  std::string read_byte_to_string(std::ifstream &rdb);

public:
  RDB_Decoder(const DB_Config &t_config) : config(t_config){};
  int read_rdb();
};
