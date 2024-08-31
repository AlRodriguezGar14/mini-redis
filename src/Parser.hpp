#pragma once

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

enum class RespType {
  SimpleString,
  SimpleError,
  Integer,
  BulkString,
  Array,
  Null,
  Boolean,
  Double,
  BigNumber,
  BulkError,
  VerbatimString,
  Map,
  Set,
  Push
};

class RespData {
public:
  RespType type;
  std::variant<std::string, int64_t, double, bool, std::vector<RespData>,
               std::map<RespData, RespData>, std::set<RespData>, std::monostate>
      value;

  RespData() : type(RespType::Null), value(std::monostate{}) {}

  template <typename T> RespData(RespType t, T v) : type(t), value(v) {}

  RespData(RespType t, std::nullptr_t) : type(t), value(std::monostate{}) {}

  RespData(const RespData &) = default;
  RespData &operator=(const RespData &) = default;

  bool operator<(const RespData &other) const {
    if (type != other.type)
      return type < other.type;
    return value < other.value;
  }
};

class RespParser {
public:
  RespData parse(const std::string &input);
  void printRespData(const RespData &data, int indent = 0);

private:
  RespData parseValue(const std::string &input, size_t &pos);

  std::string readLine(const std::string &input, size_t &pos);

  RespData parseSimpleString(const std::string &input, size_t &pos);

  RespData parseSimpleError(const std::string &input, size_t &pos);

  RespData parseInteger(const std::string &input, size_t &pos);

  RespData parseBulkString(const std::string &input, size_t &pos);

  RespData parseArray(const std::string &input, size_t &pos);

  RespData parseNull(const std::string &input, size_t &pos);

  RespData parseBoolean(const std::string &input, size_t &pos);

  RespData parseDouble(const std::string &input, size_t &pos);

  RespData parseBigNumber(const std::string &input, size_t &pos);

  RespData parseBulkError(const std::string &input, size_t &pos);

  RespData parseVerbatimString(const std::string &input, size_t &pos);

  RespData parseMap(const std::string &input, size_t &pos);

  RespData parseSet(const std::string &input, size_t &pos);

  RespData parsePush(const std::string &input, size_t &pos);
};
