#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::tfi {

/// TFM Music Maker / VGMrips .tfi binary format — 42 bytes, one
/// instrument per file.
///
/// Designed for YM2203 and compatible with the YM2612 FM core, so every
/// field maps 1:1 onto the Patch operator/channel parameters without
/// loss.  Fields the format does NOT represent and therefore silently
/// drop on write (mirroring the DMP convention in this codebase):
///
///   - patch name (use the filename to reconstruct)
///   - L/R panning, DAC enable
///   - LFO enable/frequency, channel AMS/FMS
///   - per-operator AM enable (AMS-EN)
///
/// Byte layout:
///
///   0x00: Algorithm (0-7)
///   0x01: Feedback  (0-7)
///   0x02-0x0B: OP0 (10 bytes)
///   0x0C-0x15: OP1
///   0x16-0x1F: OP2
///   0x20-0x29: OP3
///
/// Each operator block (10 bytes):
///
///   +0 MUL (0-15)
///   +1 Detune (0..3..6 linear = -3..0..+3; DMP-style, converted via
///              detune_from_linear / detune_to_linear)
///   +2 TL  (0-127)
///   +3 RS  (0-3, aka KS)
///   +4 AR  (0-31)
///   +5 DR  (0-31)
///   +6 SR  (0-31, 2nd decay)
///   +7 RR  (0-15)
///   +8 SL  (0-15)
///   +9 SSG-EG (0 or 8-15; bit3 = enable, bits 0-2 = mode)

FormatInfo info();

/// Parse a 42-byte TFI file.  Returns a single Patch.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a Patch to the 42-byte TFI format.  Fields the format
/// cannot represent (name, pan, LFO, AMS/FMS, per-op AM-EN) are
/// silently dropped, matching the convention of other serializers
/// in this library.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::tfi
