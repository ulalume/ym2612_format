#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::ctrmml {

/// ctrmml (ctr's MML) format — plain-text Music Macro Language instrument
/// definitions.
///
/// Format:
///   @N fm ALG FB        ; instrument name
///   AR DR SR RR SL TL KS ML DT SSG   ; OP1-OP4 (4 lines)

FormatInfo info();

/// Parse MML text data.  May return multiple patches from one file.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a patch to MML text.
SerializeTextResult serialize_text(const Patch &patch);

/// Serialize to bytes (wraps serialize_text).
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::ctrmml
