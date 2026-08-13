#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::dmp {

/// DefleMask Preset (.dmp) format for YM2612 FM instruments.
///
/// Binary format: 7-byte header + 4 operators × 11 bytes = 51 bytes.
/// Supports version 0x09 (legacy) and 0x0B (modern, DefleMask's final
/// format); older versions parse best-effort with a warning, newer are
/// rejected.  There is no magic, so the sniff is the exact FM size
/// plus full field range validation: non-FM (STD) presets, size
/// mismatches, and out-of-range fields are rejected rather than
/// parsed as garbage.  Non-Genesis FM systems (e.g. Arcade/YM2151)
/// import best-effort with a warning.  A 49-byte headerless variant
/// (three leading zero bytes) is repaired heuristically.

FormatInfo info();

/// Parse DMP data from raw bytes.  The optional `name` is used as the patch
/// name when one cannot be derived from the data itself.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Parse a file explicitly identified as DMP, accepting truncated legacy
/// presets by padding the missing tail bytes with zeros.  Unlike parse(), this
/// is not suitable for format sniffing because DMP has no magic signature.
ParseResult parse_compatible(const uint8_t *data, size_t size,
                             const std::string &name = "");

/// Serialize a patch to DMP binary format.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::dmp
