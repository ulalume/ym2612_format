#pragma once

#include "result.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace ym2612_format {

/// Identifies a supported format.
enum class Format {
  Dmp,     ///< DefleMask Preset (.dmp)
  Dmf,     ///< DefleMask Module (.dmf) — read-only, extracts FM instruments
  Fui,     ///< Furnace Instrument (.fui)
  Gin,     ///< GIN JSON (.gin)
  Ginpkg,  ///< GINPKG ZIP container (.ginpkg)
  Rym2612, ///< RYM2612 Preset (.rym2612)
  Mml,     ///< ctrmml MML (.mml)
};

/// Convert a string (extension with or without dot) to a Format.
/// Returns std::nullopt if the string is not recognized.
std::optional<Format> format_from_string(const std::string &s);

/// Convert a Format to its file extension (without dot).
const char *format_to_extension(Format f);

/// Metadata about a supported format.
struct FormatInfo {
  Format format;
  std::string name;      ///< Display name (e.g. "DefleMask Preset")
  std::string extension; ///< File extension without dot (e.g. "dmp")
  bool can_read = false;
  bool can_write = false;
  bool is_text = false; ///< True for text-based formats (MML, etc.)
};

} // namespace ym2612_format
