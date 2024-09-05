#include "RDB_Decoder.hpp"
#include <cstdint>

#define DEBUG_RDB 0

/*
Length encoding is used to store the length of the next object in the stream.
Length encoding is a variable byte encoding designed to use as few bytes as
possible.

This is how length encoding works : Read one byte from the stream, compare the
two most significant bits:

Bits  How to parse - read the two most important bits first
00    The next 6 bits represent the length
01    Read one additional byte. The combined 14 bits represent the length
10    Discard the remaining 6 bits. The next 4 bytes from the stream represent
      the length
11    The next object is encoded in a special format. The remaining 6
      bits indicate the format. May be used to store numbers or Strings, see
      String Encoding

As a result of this encoding:
Numbers up to and including 63 can be stored in 1 byte
Numbers up to and including 16383 can be stored in 2 bytes
Numbers up to 2^32 -1 can be stored in 4 bytes
*/

template <typename T = unsigned char> T read(std::ifstream &rdb) {
  T val;
  rdb.read(reinterpret_cast<char *>(&val), sizeof(val));
  return val;
}

std::pair<std::optional<uint64_t>, std::optional<int8_t>>
RDB_Decoder::get_str_bytes_len(std::ifstream &rdb) {
  // Read the first byte from the RDB file
  auto byte = read<uint8_t>(rdb);

  // Get the two most significant bits of the byte
  // These bits determine how the length is encoded
  auto sig = byte >> 6; // 0 bytes, 1, 2, 3 - 00, 01, 10, 11

  switch (sig) {
  case 0: {
    // If the two most significant bits are 00
    // The length is the lower 6 bits of the byte
    return {byte & 0x3F, std::nullopt};
  }
  case 1: {
    // If the two most significant bits are 01
    // The length is the lower 6 bits of the first byte and the whole next byte
    auto next_byte = read<uint8_t>(rdb);
    uint64_t sz = ((byte & 0x3F) << 8) | next_byte;
    return {sz, std::nullopt};
  }
  case 2: {
    // If the two most significant bits are 10
    // The length is the next 4 bytes
    uint64_t sz = 0;
    for (int i = 0; i < 4; i++) {
      auto byte = read<uint8_t>(rdb);
      sz = (sz << 8) | byte;
    }
    return {sz, std::nullopt};
  }
  case 3: {
    // If the two most significant bits are 11
    // The string is encoded as an integer
    switch (byte) {
    case 0xC0:
      // The string is encoded as an 8-bit integer of 1 byte
      return {std::nullopt, 8};
    case 0xC1:
      // The string is encoded as a 16-bit integer of 2 bytes
      return {std::nullopt, 16};
    case 0xC2:
      // The string is encoded as a 32-bit integer of 4 bytes
      return {std::nullopt, 32};
    case 0xFD:
      // Special case for database sizes
      return {byte, std::nullopt};
    default:
      return {std::nullopt, 0};
    }
  }
  }
  return {std::nullopt, 0};
}

std::string RDB_Decoder::read_byte_to_string(std::ifstream &rdb) {
  std::pair<std::optional<uint64_t>, std::optional<int8_t>> decoded_size =
      get_str_bytes_len(rdb);

  if (decoded_size.first.has_value()) { // the length of the string is prefixed
    int size = decoded_size.first.value();
    std::vector<char> buffer(size);
    rdb.read(buffer.data(), size);
    return std::string(buffer.data(), size);
  }
  assert(
      decoded_size.second.has_value()); // the string is encoded as an integer
  int type = decoded_size.second.value();
  switch (type) {
  case 8: {
    auto val = read<int8_t>(rdb);
    return std::to_string(val);
  }
  case 16: { // 16 bit integer, 2 bytes
    auto val = read<int16_t>(rdb);
    return std::to_string(val);
  }
  case 32: { // 32 bit integer, 4 bytes
    auto val = read<int32_t>(rdb);
    return std::to_string(val);
  }
  default:
    return "";
  }
}

// encoding -> https://rdb.fnordig.de/file_format.html#length-encoding
// https://github.com/sripathikrishnan/redis-rdb-tools/wiki/Redis-RDB-Dump-File-Format
// https://app.codecrafters.io/courses/redis/stages/jz6
// https://rdb.fnordig.de/file_format.html
/*
At a high level, the RDB file has the following structure

----------------------------#
52 45 44 49 53              # Magic String "REDIS"
30 30 30 33                 # RDB Version Number as ASCII string. "0003" = 3
----------------------------
FA                          # Auxiliary field
$string-encoded-key         # May contain arbitrary metadata
$string-encoded-value       # such as Redis version, creation time, used
memory,
...
----------------------------
FE 00                       # Indicates database selector. db number = 00
FB                          # Indicates a resizedb field
$length-encoded-int         # Size of the corresponding hash table
$length-encoded-int         # Size of the corresponding expire hash table
----------------------------# Key-Value pair starts
FD $unsigned-int            # "expiry time in seconds", followed by 4 byte
unsigned int $value-type                 # 1 byte flag indicating the type of
value $string-encoded-key         # The key, encoded as a redis string
$encoded-value              # The value, encoding depends on $value-type
----------------------------
FC $unsigned long           # "expiry time in ms", followed by 8 byte unsigned
long $value-type                 # 1 byte flag indicating the type of value
$string-encoded-key         # The key, encoded as a redis string
$encoded-value              # The value, encoding depends on $value-type
----------------------------
$value-type                 # key-value pair without expiry
$string-encoded-key
$encoded-value
----------------------------
FE $length-encoding         # Previous db ends, next db starts.
----------------------------
...                         # Additional key-value pairs, databases, ...

FF                          ## End of RDB file indicator
8-byte-checksum             ## CRC64 checksum of the entire file.
 */
int RDB_Decoder::read_rdb() {
  config.file = config.dir + "/" + config.db_filename;
  std::ifstream rdb(config.file, std::ios::binary);
  if (!rdb.is_open()) {
    std::cerr << "Could not open the Redis Persistent Database: " << config.file
              << std::endl;
    return 0;
  }

  char header[9];
  rdb.read(header, 9);
  if (DEBUG_RDB != 0)
    std::cout << "Header: " << std::string(header, 9) << std::endl;

  // metadata
  while (true) {
    unsigned char opcode;
    if (!rdb.read(reinterpret_cast<char *>(&opcode), 1)) {
      if (DEBUG_RDB != 0)
        std::cout << "Reached end of file while looking for database start"
                  << std::endl;
      return 0;
    }

    if (opcode == 0xFA) {
      std::string key = read_byte_to_string(rdb);
      std::string value = read_byte_to_string(rdb);
      if (DEBUG_RDB != 0)
        std::cout << "AUX: " << key << " " << value << std::endl;
    }
    if (opcode == 0xFE) {
      auto db_number = get_str_bytes_len(rdb);
      if (db_number.first.has_value()) {
        if (DEBUG_RDB != 0)
          std::cout << "SELECTDB: Database number: " << db_number.first.value()
                    << std::endl;
        opcode = read<unsigned char>(
            rdb); // Read the next opcode after the database number
      }
    }
    if (opcode == 0xFB) {
      auto hash_table_size = get_str_bytes_len(rdb);
      auto expire_hash_table_size = get_str_bytes_len(rdb);
      if (hash_table_size.first.has_value() &&
          expire_hash_table_size.first.has_value()) {
        if (DEBUG_RDB != 0)
          std::cout << "RESIZEDB: Hash table size: "
                    << hash_table_size.first.value()
                    << ", Expire hash table size: "
                    << expire_hash_table_size.first.value() << std::endl;
      }
      break;
    }
  }

  // Read key-value pairs
  while (true) {
    unsigned char opcode;
    if (!rdb.read(reinterpret_cast<char *>(&opcode), 1)) {
      if (DEBUG_RDB != 0)
        std::cout << "Reached end of file" << std::endl;
      break;
    }

    if (opcode == 0xFF) {
      if (DEBUG_RDB != 0)
        std::cout << "EOF: Reached end of database marker" << std::endl;
      uint64_t checksum;
      checksum = read<uint64_t>(rdb);
      if (DEBUG_RDB != 0)
        std::cout << "db checksum: " << checksum << std::endl;
      break;
    }

    uint64_t expire_time_s = 0;
    uint64_t expire_time_ms = 0;
    if (opcode ==
        0xFD) { // expiry time in seconds followed by 4 byte - uint32_t
      uint32_t seconds;
      rdb.read(reinterpret_cast<char *>(&seconds), sizeof(seconds));
      expire_time_s = read<uint32_t>(rdb);
      opcode = read<uint8_t>(rdb);
      // rdb.read(reinterpret_cast<char *>(&opcode), 1);
      if (DEBUG_RDB != 0)
        std::cout << "EXPIRETIME: " << expire_time_ms << std::endl;
    }
    if (opcode == 0xFC) { // expiry time in ms, followd by 8 byte
                          // unsigned - uint64_t
      expire_time_ms = read<uint64_t>(rdb);
      opcode = read<uint8_t>(rdb);
      if (DEBUG_RDB != 0)
        std::cout << "EXPIRETIMEMS: " << expire_time_ms << std::endl;
    }

    // After 0xFD and 0x FC, comes the key-pair-value
    std::string key = read_byte_to_string(rdb);
    std::string value = read_byte_to_string(rdb);

    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    if (expire_time_s == 0 || expire_time_ms > now) {
      if (DEBUG_RDB != 0)
        std::cout << "adding " << key << " - " << value << std::endl;
      config.db.insert_or_assign(key, DB_Entry({value, 0, expire_time_ms}));
    }
  }

  rdb.close();
  return 0;
}
