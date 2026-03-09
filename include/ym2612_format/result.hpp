#pragma once

#include "patch.hpp"
#include <string>
#include <variant>
#include <vector>

namespace ym2612_format {

/// Successful parse result containing one or more patches and optional
/// warnings.
struct ParseOk {
  std::vector<Patch> patches;
  std::vector<std::string> warnings;
};

/// A parse or serialize error.
struct Error {
  std::string message;
};

/// Result of a parse operation.
using ParseResult = std::variant<ParseOk, Error>;

/// Result of a serialize operation.
using SerializeResult = std::variant<std::vector<uint8_t>, Error>;

/// Result of a text serialize operation (for text-based formats).
using SerializeTextResult = std::variant<std::string, Error>;

// Helper functions

inline bool is_ok(const ParseResult &r) {
  return std::holds_alternative<ParseOk>(r);
}

inline bool is_ok(const SerializeResult &r) {
  return std::holds_alternative<std::vector<uint8_t>>(r);
}

inline bool is_ok(const SerializeTextResult &r) {
  return std::holds_alternative<std::string>(r);
}

inline const ParseOk &get_ok(const ParseResult &r) {
  return std::get<ParseOk>(r);
}

inline const std::vector<uint8_t> &get_ok(const SerializeResult &r) {
  return std::get<std::vector<uint8_t>>(r);
}

inline const std::string &get_ok(const SerializeTextResult &r) {
  return std::get<std::string>(r);
}

inline const Error &get_error(const ParseResult &r) {
  return std::get<Error>(r);
}

inline const Error &get_error(const SerializeResult &r) {
  return std::get<Error>(r);
}

inline const Error &get_error(const SerializeTextResult &r) {
  return std::get<Error>(r);
}

} // namespace ym2612_format
