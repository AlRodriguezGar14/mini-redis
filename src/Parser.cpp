#include "Parser.hpp"

RespData RespParser::parse(const std::string &input) {
  size_t pos = 0;
  return parseValue(input, pos);
}

RespData RespParser::parseValue(const std::string &input, size_t &pos) {
  if (pos >= input.length()) {
    throw std::runtime_error("Unexpected end of input");
  }

  char type = input[pos++];
  switch (type) {
  case '*':
    return parseArray(input, pos);
  case '+':
    return parseSimpleString(input, pos);
  case '-':
    return parseSimpleError(input, pos);
  case ':':
    return parseInteger(input, pos);
  case '$':
    return parseBulkString(input, pos);
  case '_':
    return parseNull(input, pos);
  case '#':
    return parseBoolean(input, pos);
  case ',':
    return parseDouble(input, pos);
  case '(':
    return parseBigNumber(input, pos);
  case '!':
    return parseBulkError(input, pos);
  case '=':
    return parseVerbatimString(input, pos);
  case '%':
    return parseMap(input, pos);
  case '~':
    return parseSet(input, pos);
  case '>':
    return parsePush(input, pos);
  default:
    throw std::runtime_error("Unknown type identifier: " +
                             std::string(1, type));
  }
}

std::string RespParser::readLine(const std::string &input, size_t &pos) {
  size_t end = input.find("\r\n", pos);
  if (end == std::string::npos) {
    throw std::runtime_error("Expected CRLF");
  }
  std::string line = input.substr(pos, end - pos);
  pos = end + 2;
  return line;
}

RespData RespParser::parseSimpleString(const std::string &input, size_t &pos) {
  return RespData(RespType::SimpleString, readLine(input, pos));
}

RespData RespParser::parseSimpleError(const std::string &input, size_t &pos) {
  return RespData(RespType::SimpleError, readLine(input, pos));
}

RespData RespParser::parseInteger(const std::string &input, size_t &pos) {
  return RespData(RespType::Integer, std::stoll(readLine(input, pos)));
}

RespData RespParser::parseBulkString(const std::string &input, size_t &pos) {
  int64_t len = std::stoll(readLine(input, pos));
  if (len == -1) {
    return RespData(RespType::Null, nullptr);
  }
  if (pos + len > input.length()) {
    throw std::runtime_error("Bulk string length exceeds input");
  }
  std::string str = input.substr(pos, len);
  pos += len + 2; // +2 for CRLF
  return RespData(RespType::BulkString, str);
}

RespData RespParser::parseArray(const std::string &input, size_t &pos) {
  int64_t len = std::stoll(readLine(input, pos));
  std::vector<RespData> arr;
  for (int i = 0; i < len; ++i) {
    arr.push_back(parseValue(input, pos));
  }
  return RespData(RespType::Array, arr);
}

RespData RespParser::parseNull(const std::string &input, size_t &pos) {
  readLine(input, pos); // Consume the line
  return RespData(RespType::Null, nullptr);
}

RespData RespParser::parseBoolean(const std::string &input, size_t &pos) {
  std::string value = readLine(input, pos);
  if (value == "t")
    return RespData(RespType::Boolean, true);
  if (value == "f")
    return RespData(RespType::Boolean, false);
  throw std::runtime_error("Invalid boolean value: " + value);
}

RespData RespParser::parseDouble(const std::string &input, size_t &pos) {
  return RespData(RespType::Double, std::stod(readLine(input, pos)));
}

RespData RespParser::parseBigNumber(const std::string &input, size_t &pos) {
  return RespData(RespType::BigNumber, readLine(input, pos));
}

RespData RespParser::parseBulkError(const std::string &input, size_t &pos) {
  return RespData(RespType::BulkError, parseBulkString(input, pos).value);
}

RespData RespParser::parseVerbatimString(const std::string &input,
                                         size_t &pos) {
  return RespData(RespType::VerbatimString, parseBulkString(input, pos).value);
}

RespData RespParser::parseMap(const std::string &input, size_t &pos) {
  int64_t len = std::stoll(readLine(input, pos));
  std::map<RespData, RespData> map;
  for (int i = 0; i < len; ++i) {
    RespData key = parseValue(input, pos);
    RespData value = parseValue(input, pos);
    map[key] = value;
  }
  return RespData(RespType::Map, map);
}

RespData RespParser::parseSet(const std::string &input, size_t &pos) {
  int64_t len = std::stoll(readLine(input, pos));
  std::set<RespData> set;
  for (int i = 0; i < len; ++i) {
    set.insert(parseValue(input, pos));
  }
  return RespData(RespType::Set, set);
}

RespData RespParser::parsePush(const std::string &input, size_t &pos) {
  return RespData(RespType::Push, parseArray(input, pos).value);
}

void RespParser::printRespData(const RespData &data, int indent) {
  std::string indentStr(indent * 2, ' ');
  std::cout << indentStr;

  switch (data.type) {
  case RespType::Array:
    std::cout << "Array:" << std::endl;
    for (const auto &item : std::get<std::vector<RespData>>(data.value)) {
      printRespData(item, indent + 1);
    }
    break;
  case RespType::SimpleString:
    std::cout << "SimpleString: " << std::get<std::string>(data.value)
              << std::endl;
    break;
  case RespType::SimpleError:
    std::cout << "SimpleError: " << std::get<std::string>(data.value)
              << std::endl;
    break;
  case RespType::Integer:
    std::cout << "Integer: " << std::get<int64_t>(data.value) << std::endl;
    break;
  case RespType::BulkString:
    std::cout << "BulkString: " << std::get<std::string>(data.value)
              << std::endl;
    break;
  case RespType::Null:
    std::cout << "Null" << std::endl;
    break;
  case RespType::Boolean:
    std::cout << "Boolean: " << (std::get<bool>(data.value) ? "true" : "false")
              << std::endl;
    break;
  case RespType::Double:
    std::cout << "Double: " << std::get<double>(data.value) << std::endl;
    break;
  case RespType::BigNumber:
    std::cout << "BigNumber: " << std::get<std::string>(data.value)
              << std::endl;
    break;
  case RespType::BulkError:
    std::cout << "BulkError: " << std::get<std::string>(data.value)
              << std::endl;
    break;
  case RespType::VerbatimString:
    std::cout << "VerbatimString: " << std::get<std::string>(data.value)
              << std::endl;
    break;
  case RespType::Map:
    std::cout << "Map:" << std::endl;
    for (const auto &[key, value] :
         std::get<std::map<RespData, RespData>>(data.value)) {
      std::cout << indentStr << "  Key: ";
      printRespData(key, 0);
      std::cout << indentStr << "  Value: ";
      printRespData(value, indent + 2);
    }
    break;
  case RespType::Set:
    std::cout << "Set:" << std::endl;
    for (const auto &item : std::get<std::set<RespData>>(data.value)) {
      printRespData(item, indent + 1);
    }
    break;
  case RespType::Push:
    std::cout << "Push:" << std::endl;
    for (const auto &item : std::get<std::vector<RespData>>(data.value)) {
      printRespData(item, indent + 1);
    }
    break;
  }
}
