#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::rym2612 {

/// RYM2612 format — XML-based preset from the RYM2612 editor.
///
/// Read-only.  Uses `<PARAM id="..." value="..."/>` elements.

FormatInfo info();

/// Parse RYM2612 XML data (UTF-8 text as raw bytes).
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

} // namespace ym2612_format::rym2612
