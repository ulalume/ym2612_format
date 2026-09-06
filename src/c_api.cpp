#include "ym2612_format/c_api.h"

#include "ym2612_format/converter.hpp"
#include "ym2612_format/ctrmml.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#ifndef YM2612_FORMAT_VERSION
#define YM2612_FORMAT_VERSION "0.0.0"
#endif

namespace {

using namespace ym2612_format;

/// malloc-owned copy; nullptr when the allocation fails.
char *dup_string(const char *s, size_t size) {
  char *out = static_cast<char *>(std::malloc(size + 1));
  if (!out)
    return nullptr;
  std::memcpy(out, s, size);
  out[size] = '\0';
  return out;
}

char *dup_string(const std::string &s) { return dup_string(s.data(), s.size()); }

/// Does not throw; usable inside a catch handler.
void set_error(char **error, const char *message) {
  if (error)
    *error = dup_string(message, std::strlen(message));
}

void set_error(char **error, const std::string &message) {
  if (error)
    *error = dup_string(message);
}

void json_string(std::string &out, const std::string &s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
    case '"':  out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\b': out += "\\b"; break;
    case '\f': out += "\\f"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 0x20) {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  out += '"';
}

/// Format hint taken from the extension of a file name.
std::optional<Format> format_from_name(const char *name) {
  if (!name)
    return std::nullopt;
  std::string s(name);
  size_t start = s.find_last_of("/\\");
  size_t dot = s.find_last_of('.');
  if (dot == std::string::npos ||
      (start != std::string::npos && dot < start) || dot + 1 == s.size())
    return std::nullopt;
  return format_from_string(s.substr(dot + 1));
}

/// Explicit `format` wins; NULL falls back to the extension of `name`.
/// False when `format` names no known format.
bool select_format(const char *format, const char *name,
                   std::optional<Format> &out, std::string &error_message) {
  if (format) {
    auto f = format_from_string(format);
    if (!f) {
      error_message = std::string("Unknown format '") + format + "'";
      return false;
    }
    out = f;
    return true;
  }
  out = format_from_name(name);
  return true;
}

/// Parse and reject empty results; `out` is valid only when true.
bool parse_patches(const uint8_t *data, size_t size, const char *name,
                   const char *format, ParseOk &out, char **error) {
  std::optional<Format> hint;
  std::string message;
  if (!select_format(format, name, hint, message)) {
    set_error(error, message);
    return false;
  }
  auto result = parse(data, size, hint, name ? name : "");
  if (!is_ok(result)) {
    set_error(error, get_error(result).message);
    return false;
  }
  out = get_ok(result);
  if (out.patches.empty()) {
    set_error(error, "No patches found");
    return false;
  }
  return true;
}

} // namespace

extern "C" {

const char *ym2612_version(void) { return YM2612_FORMAT_VERSION; }

char *ym2612_formats_json(void) {
  try {
    std::string out = "[";
    bool first = true;
    for (const auto &info : all_formats()) {
      if (!first)
        out += ',';
      first = false;
      out += "{\"format\":";
      json_string(out, format_to_extension(info.format));
      out += ",\"name\":";
      json_string(out, info.name);
      out += ",\"extension\":";
      json_string(out, info.extension);
      out += ",\"can_read\":";
      out += info.can_read ? "true" : "false";
      out += ",\"can_write\":";
      out += info.can_write ? "true" : "false";
      out += ",\"is_text\":";
      out += info.is_text ? "true" : "false";
      out += '}';
    }
    out += ']';
    return dup_string(out);
  } catch (...) {
    return nullptr;
  }
}

char *ym2612_parse_json(const uint8_t *data, size_t size, const char *name,
                        const char *format, char **error) {
  try {
    ParseOk parsed;
    if (!parse_patches(data, size, name, format, parsed, error))
      return nullptr;

    std::string out = "{\"patches\":[";
    bool first = true;
    for (const auto &patch : parsed.patches) {
      if (!first)
        out += ',';
      first = false;
      out += "{\"name\":";
      json_string(out, patch.name);
      out += ",\"algorithm\":" + std::to_string(patch.algorithm);
      out += ",\"feedback\":" + std::to_string(patch.feedback);
      out += ",\"has_macros\":";
      out += patch.has_macros() ? "true" : "false";
      out += ",\"mml\":";
      auto mml = ctrmml::serialize_text(patch);
      if (is_ok(mml))
        json_string(out, get_ok(mml));
      else
        out += "null";
      out += '}';
    }
    out += "],\"warnings\":[";
    first = true;
    for (const auto &warning : parsed.warnings) {
      if (!first)
        out += ',';
      first = false;
      json_string(out, warning);
    }
    out += "]}";

    char *result = dup_string(out);
    if (!result)
      set_error(error, "Out of memory");
    return result;
  } catch (const std::exception &e) {
    set_error(error, e.what());
    return nullptr;
  } catch (...) {
    set_error(error, "Unknown error");
    return nullptr;
  }
}

uint8_t *ym2612_convert(const uint8_t *data, size_t size, const char *name,
                        const char *format, size_t index,
                        const char *target_format, size_t *out_size,
                        char **error) {
  if (out_size)
    *out_size = 0;
  try {
    if (!target_format) {
      set_error(error, "Target format is required");
      return nullptr;
    }
    auto target = format_from_string(target_format);
    if (!target) {
      set_error(error, std::string("Unknown format '") + target_format + "'");
      return nullptr;
    }

    ParseOk parsed;
    if (!parse_patches(data, size, name, format, parsed, error))
      return nullptr;
    if (index >= parsed.patches.size()) {
      set_error(error, "Patch index " + std::to_string(index) +
                           " out of range (" +
                           std::to_string(parsed.patches.size()) + " patches)");
      return nullptr;
    }

    auto result = serialize(*target, parsed.patches[index]);
    if (!is_ok(result)) {
      set_error(error, get_error(result).message);
      return nullptr;
    }

    const auto &bytes = get_ok(result);
    auto *out =
        static_cast<uint8_t *>(std::malloc(bytes.empty() ? 1 : bytes.size()));
    if (!out) {
      set_error(error, "Out of memory");
      return nullptr;
    }
    std::memcpy(out, bytes.data(), bytes.size());
    if (out_size)
      *out_size = bytes.size();
    return out;
  } catch (const std::exception &e) {
    set_error(error, e.what());
    return nullptr;
  } catch (...) {
    set_error(error, "Unknown error");
    return nullptr;
  }
}

void ym2612_free(void *ptr) { std::free(ptr); }

} // extern "C"
