#include "HandleResponse.hpp"
#include "Parser.hpp"
#include "Server.hpp"
#include <algorithm>
#include <bits/chrono.h>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>

int HandleResponse::check_expire_ms(std::string key, DB_Config &config) {

  if (config.db[key].expiry == 0)
    return 0;
  std::cout << "Checking expiry: " << key << std::endl;
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();

  if (config.db[key].expiry > now)
    return 0;

  config.db.erase(key);
  try {
    config.in_memory_db.at(key);
    config.in_memory_db.erase(key);
  } catch (std::exception &e) {
  }
  null();
  return 1;
}

HandleResponse::HandleResponse(RespData result, int client_fd,
                               DB_Config &config)
    : m_client_fd(client_fd) {
  if (result.type == RespType::Array) {
    array(result, config);
  }
}

void HandleResponse::config_req(size_t &i,
                                const std::vector<RespData> &command_array,
                                const DB_Config &config) {
  if (i >= command_array.size() ||
      command_array[i].type != RespType::BulkString)
    return;

  // The only request handling is CONFIG GET
  std::string cmd = std::get<std::string>(command_array[i++].value);
  std::string what = std::get<std::string>(command_array[i++].value);
  if (cmd != "GET")
    return;

  if (what == "dir") {
    std::string response = "*2\r\n$3\r\ndir\r\n$";
    response += std::to_string(config.dir.length());
    response += "\r\n";
    response += config.dir;
    response += "\r\n";
    send(m_client_fd, response.c_str(), response.length(), 0);
  }
  if (what == "dbfilename") {
    std::string response = "*2\r\n$10\r\ndbfilename\r\n$";
    response += std::to_string(config.db_filename.length());
    response += "\r\n";
    response += config.db_filename;
    response += "\r\n";
    send(m_client_fd, response.c_str(), response.length(), 0);
  }
}

void HandleResponse::keys(size_t &i, const std::vector<RespData> &command_array,
                          DB_Config &config) {
  if (i > command_array.size() || command_array[i].type != RespType::BulkString)
    return;

  std::string cmd = std::get<std::string>(command_array[i++].value);
  std::string response;
  if (cmd == "*") {
    response += "*";
    response += std::to_string(config.db.size());
    response += "\r\n";
    for (auto &entry : config.db) {
      std::string key = entry.first;
      response += "$";
      response += std::to_string(key.length());
      response += "\r\n";
      response += key;
      response += "\r\n";
    }
  }
  send(m_client_fd, response.c_str(), response.length(), 0);
}

void HandleResponse::ping() {
  send(m_client_fd, ping_response, strlen(ping_response), 0);
}
void HandleResponse::echo(size_t &i,
                          const std::vector<RespData> &command_array) {

  if (i < command_array.size() &&
      command_array[i].type == RespType::BulkString) {

    std::string echo_data = std::get<std::string>(command_array[i++].value);
    echo_response += "$";
    echo_response += std::to_string(echo_data.length());
    echo_response += "\r\n";
    echo_response += echo_data;
    echo_response += "\r\n";
    send(m_client_fd, echo_response.c_str(), echo_response.length(), 0);
  } else {
    empty();
  }
}

void HandleResponse::empty() {
  const char *empty_response = "$0\r\n\r\n";
  send(m_client_fd, empty_response, strlen(empty_response), 0);
}

void HandleResponse::ok() {
  const char *ok_response = "+OK\r\n";
  send(m_client_fd, ok_response, strlen(ok_response), 0);
}

void HandleResponse::null() {
  const char *null_bulk_string = "$-1\r\n";
  send(m_client_fd, null_bulk_string, strlen(null_bulk_string), 0);
}

void HandleResponse::set(size_t &i, const std::vector<RespData> &command_array,
                         DB_Config &config) {
  size_t max_size = command_array.size();
  if (i >= max_size || command_array[i].type != RespType::BulkString) {
    null();
    return;
  }

  std::string key = std::get<std::string>(command_array[i++].value);
  if (i > max_size || command_array[i].type != RespType::BulkString) {
    null();
    return;
  }
  std::string value = std::get<std::string>(command_array[i++].value);
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
  DB_Entry entry{value, now, 0};
  config.db.insert_or_assign(key, entry);
  config.in_memory_db.insert_or_assign(key, entry);
  if (i >= max_size) {
    ok();
    return;
  }
  std::string next_cmd = std::get<std::string>(command_array[i].value);
  if (next_cmd != "px") {
    return;
  }
  entry.expiry =
      now +
      std::strtoull(std::get<std::string>(command_array[++i].value).c_str(),
                    NULL, 10);
  if (entry.expiry == 0)
    return;
  // std::cout << "Assigned to db_expiry[" << key << "] : " << entry.expiry
  //           << "/ms" << std::endl;
  config.db.insert_or_assign(key, entry);
  config.in_memory_db.insert_or_assign(key, entry);
  ok();
}

int HandleResponse::send_entry(DB_Config &config, const std::string &key) {
  try {
    config.db.at(key);
    if (check_expire_ms(key, config) == 1)
      return -1;
    std::string value = config.db[key].value;
    // std::cout << "Delivering: " << key << std::endl;
    std::string response = "$";
    response += std::to_string(value.length());
    response += "\r\n";
    response += value;
    response += "\r\n";
    send(m_client_fd, response.c_str(), response.length(), 0);
  } catch (std::exception &e) {
    std::cerr << "Map error, key not found: " << e.what() << std::endl;
    null();
    return -1;
  }
  return 0;
}

void HandleResponse::get(size_t &i, const std::vector<RespData> &command_array,
                         DB_Config &config) {
  size_t max_size = command_array.size();
  if (i > max_size || command_array[i].type != RespType::BulkString) {
    null();
    return;
  }
  std::string key = std::get<std::string>(command_array[i++].value);
  send_entry(config, key);
  return;
}

void HandleResponse::array(RespData result, DB_Config &config) {
  // Extract array
  const auto &command_array = std::get<std::vector<RespData>>(result.value);
  for (size_t i = 0; i <= command_array.size(); ++i) {
    const auto &command = command_array[i];
    // Extract each command based on type std::get()
    if (command.type == RespType::BulkString) {
      std::string command_str = std::get<std::string>(command.value);
      std::transform(command_str.begin(), command_str.end(),
                     command_str.begin(), ::toupper);

      if (command_str == "PING") {
        ping();
        ++i;
      }
      if (command_str == "ECHO") {
        echo(++i, command_array);
      }
      if (command_str == "SET") {
        set(++i, command_array, config);
      }
      if (command_str == "GET") {
        get(++i, command_array, config);
      }
      if (command_str == "CONFIG") {
        config_req(++i, command_array, config);
      }
      if (command_str == "KEYS") {
        keys(++i, command_array, config);
      }
    } else {
      ++i;
    }
  }
}
