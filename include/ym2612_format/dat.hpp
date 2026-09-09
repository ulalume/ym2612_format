#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::dat {

/// YM2612 Instrument Editor .dat FM instrument format — read-only, one
/// instrument per file.  Instead of a fixed struct the file is a list of
/// YM2612 register writes: a 16-bit big-endian pair count, then that
/// many (register address, value) byte pairs.
///
///   0x00-0x01: pair count N, big-endian
///   0x02+:     N pairs of (register address, value)
///
/// Addresses ascend strictly and are limited to the per-operator
/// registers $30-$9F with the low two bits selecting the operator
/// ((address >> 2) & 3, register-slot order, matching this library's
/// Patch::operators[] convention), plus the channel registers $B0
/// (Algorithm | Feedback << 3) and $B4 (FMS | AMS << 4).  $B0 is always
/// present.  Registers absent from the list keep their Patch defaults.
///
/// Values are stored exactly as written to the chip, so Detune keeps its
/// full hardware encoding (0-7) — unlike the linear encoding used by
/// TFI/VGI.  The list carries no patch name, no L/R panning (the $B4
/// panning bits are ignored), no DAC enable and no LFO.

FormatInfo info();

/// Parse a YM2612 Instrument Editor file.  Returns a single Patch.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

} // namespace ym2612_format::dat
