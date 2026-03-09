#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::ginpkg {

/// GINPKG format — a ZIP container holding a GIN (JSON) patch as
/// `current.gin`.
///
/// Read-only.  History / versioning data inside the package is ignored.

FormatInfo info();

/// Parse GINPKG data (raw ZIP bytes).  Extracts `current.gin` and parses
/// it as GIN format.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

} // namespace ym2612_format::ginpkg
