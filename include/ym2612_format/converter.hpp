#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ym2612_format {

/// Returns information about all built-in formats.
std::vector<FormatInfo> all_formats();

/// Parse data with automatic format detection.
///
/// Tries each format's parser in order; returns the first successful result.
/// If `hint` is provided, that format is tried first.
///
/// `name` is passed through to the format parser as a fallback patch name.
ParseResult parse(const uint8_t *data, size_t size,
                  std::optional<Format> hint = std::nullopt,
                  const std::string &name = "");

/// Parse data with a specific format.
ParseResult parse_as(Format format, const uint8_t *data, size_t size,
                     const std::string &name = "");

/// Serialize a patch to a specific format.
SerializeResult serialize(Format format, const Patch &patch);

/// Serialize a patch to a text format (returns string).
/// Only works for text-based formats like MML.
SerializeTextResult serialize_text(Format format, const Patch &patch);

} // namespace ym2612_format
