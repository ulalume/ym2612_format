#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::dmp {

/// DefleMask Preset (.dmp) format for YM2612 FM instruments.
///
/// Binary format: 7-byte header + 4 operators × 11 bytes = 51 bytes.
/// Supports version 0x09 (legacy) and 0x0B (modern).

FormatInfo info();

/// Parse DMP data from raw bytes.  The optional `name` is used as the patch
/// name when one cannot be derived from the data itself.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a patch to DMP binary format.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::dmp
