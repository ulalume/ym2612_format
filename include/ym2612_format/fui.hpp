#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::fui {

/// Furnace Instrument (.fui) format.
///
/// Supports both the modern FINS-based format and the legacy pre-1.0 format.
/// Modern format uses feature blocks (NA, FM, EN).

FormatInfo info();

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::fui
