#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::dmf {

/// DefleMask Module Format (.dmf) — extracts FM instruments from zlib-compressed
/// module files.  Read-only.  Supports version 0x15 (DefleMask 11.1) and 0x18
/// (DefleMask v1.0.0).

FormatInfo info();

/// Parse a DMF file and extract all FM instruments as patches.
/// The input data should be the raw .dmf file (zlib-compressed).
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

} // namespace ym2612_format::dmf
