#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::fur {

/// Furnace Module Format (.fur) — extracts FM instruments from Furnace module
/// files. Read-only. Supports format version >= 127 (new instrument format).
/// Instruments are extracted as FM patches with macros (if present).

FormatInfo info();

/// Parse a FUR file and extract all FM (OPN) instruments as patches.
/// The input data may be zlib-compressed.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

} // namespace ym2612_format::fur
