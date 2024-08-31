#include "HandleResponse.hpp"
#include "Parser.hpp"

HandleResponse::HandleResponse(RespData result, int client_fd,
                               std::map<std::string, std::string> &db)
    : m_client_fd(client_fd) {
  if (result.type == RespType::Array) {
    array(result, db);
  }
}

void HandleResponse::array(RespData result,
                           std::map<std::string, std::string> &db) {
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
        set(++i, command_array, db);
      }
      if (command_str == "GET") {
        get(++i, command_array, db);
      }
    } else {
      ++i;
    }
  }
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
                         std::map<std::string, std::string> &db) {
  if (i >= command_array.size() ||
      command_array[i].type != RespType::BulkString) {
    null();
    return;
  }

  std::string key = std::get<std::string>(command_array[i++].value);
  if (i > command_array.size() ||
      command_array[i].type != RespType::BulkString) {
    null();
    return;
  }
  std::string value = std::get<std::string>(command_array[i++].value);
  db.insert_or_assign(key, value);
  std::cout << "Set map[" << key << "] = " << value << std::endl;
  ok();
}

void HandleResponse::get(size_t &i, const std::vector<RespData> &command_array,
                         std::map<std::string, std::string> &db) {
  if (i > command_array.size() ||
      command_array[i].type != RespType::BulkString) {
    null();
    return;
  }
  std::string key = std::get<std::string>(command_array[i++].value);
  try {
    db.at(key);
    std::string value = db[key];
    std::string response = "$";
    response += std::to_string(value.length());
    response += "\r\n";
    response += value;
    response += "\r\n";
    send(m_client_fd, response.c_str(), response.length(), 0);
  } catch (std::exception &e) {
    std::cerr << "Map error: " << e.what() << std::endl;
    null();
  }
  return;
}
