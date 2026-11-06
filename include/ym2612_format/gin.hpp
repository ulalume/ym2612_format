#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::gin {

/// GIN format — a simple JSON representation of a YM2612 patch.
///
/// Uses the same JSON schema as megatoy's native .gin format.
///
/// Not stored: the DAC enable, the LFO's enable and frequency, the L/R
/// speakers and each operator's enable, which say how a patch is played.
/// Parsing leaves them at their defaults (DAC off, LFO off at 0, both
/// speakers on, every operator on), whatever the file holds.

FormatInfo info();

/// Parse GIN (JSON) data.  Input is the raw JSON bytes (UTF-8 text).
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a patch to GIN JSON.  Returns the JSON bytes as UTF-8 text.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::gin
