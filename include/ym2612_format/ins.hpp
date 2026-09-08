#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::ins {

/// MVS Tracker MD .ins FM instrument format — read-only, one instrument
/// per file.  An ASCII "MVSI1" signature, a NUL-terminated instrument
/// name, then 25 bytes of raw YM2612 register data grouped by parameter.
///
///   0x00-0x04: ASCII "MVSI1"
///   0x05+:     instrument name, NUL-terminated
///   +0x00-0x03: MUL | DT << 4, one byte per op    (register $30+)
///   +0x04-0x07: TL                                (register $40+)
///   +0x08-0x0B: AR | RS << 6                      (register $50+)
///   +0x0C-0x0F: DR | AM-EN << 7                   (register $60+)
///   +0x10-0x13: SR                                (register $70+)
///   +0x14-0x17: RR | SL << 4                      (register $80+)
///   +0x18:      Algorithm | Feedback << 3         (register $B0)
///
/// Operators are in register-slot order, matching this library's
/// Patch::operators[] convention.  Values are stored exactly as written
/// to the chip, so Detune keeps its full hardware encoding (0-7) —
/// unlike the linear encoding used by TFI/VGI.  The format carries no
/// SSG-EG and no channel FMS/AMS, and no L/R panning, DAC enable or LFO.

FormatInfo info();

/// Parse an MVS Tracker MD file.  Returns a single Patch named after the
/// embedded instrument name.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

} // namespace ym2612_format::ins
