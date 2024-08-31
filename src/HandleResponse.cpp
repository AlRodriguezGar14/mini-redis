#include "HandleResponse.hpp"

HandleResponse::HandleResponse(RespData result, int client_fd)
    : m_client_fd(client_fd) {
  if (result.type == RespType::Array) {
    array(result);
  }
}

void HandleResponse::array(RespData result) {
  // Extract array
  const auto &command_array = std::get<std::vector<RespData>>(result.value);
  for (auto command = command_array.begin(); command != command_array.end();
       ++command) {
    // Extract each command based on type std::get()
    if (command->type == RespType::BulkString) {
      std::string command_str = std::get<std::string>(command->value);
      std::transform(command_str.begin(), command_str.end(),
                     command_str.begin(), ::toupper);

      if (command_str == "PING") {
        ping();
      }
      if (command_str == "ECHO") {
        echo(command, command_array);
      } else {
        continue;
        // empty();
      }
    }
  }
}
void HandleResponse::ping() {
  send(m_client_fd, ping_response, strlen(ping_response), 0);
}
void HandleResponse::echo(std::vector<RespData>::const_iterator command,
                          const std::vector<RespData> &command_array) {

  if (std::next(command) != command_array.end() &&
      std::next(command)->type == RespType::BulkString) {

    std::string echo_data = std::get<std::string>(std::next(command)->value);
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
