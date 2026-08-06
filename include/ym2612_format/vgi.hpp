#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::vgi {

/// VGM Music Maker .vgi binary format — 43 bytes, one instrument per
/// file.  Defined by Shiru's VGM Music Maker; also read by DefleMask
/// and Furnace.
///
/// A superset of TFI: same 10-byte operator blocks plus a third header
/// byte carrying channel FMS/AMS, and the per-operator AM-EN flag in
/// bit 7 of the DR byte.  Fields the format does NOT represent and
/// therefore silently drop on write (mirroring the TFI/DMP convention
/// in this codebase):
///
///   - patch name (use the filename to reconstruct)
///   - L/R panning, DAC enable
///   - LFO enable/frequency
///
/// Byte layout:
///
///   0x00: Algorithm (0-7)
///   0x01: Feedback  (0-7)
///   0x02: FMS (bits 0-2) | AMS << 4 (bits 4-5)
///   0x03-0x0C: OP0 (10 bytes)
///   0x0D-0x16: OP1
///   0x17-0x20: OP2
///   0x21-0x2A: OP3
///
/// Each operator block (10 bytes):
///
///   +0 MUL (0-15)
///   +1 Detune (0..3..6 linear = -3..0..+3; converted via
///              detune_from_linear / detune_to_linear)
///   +2 TL  (0-127)
///   +3 RS  (0-3, aka KS)
///   +4 AR  (0-31)
///   +5 DR  (0-31) | AM-EN << 7
///   +6 SR  (0-31, 2nd decay)
///   +7 RR  (0-15)
///   +8 SL  (0-15)
///   +9 SSG-EG (0 or 8-15; bit3 = enable, bits 0-2 = mode)
///
/// Operators are stored in register-slot order, matching this
/// library's Patch::operators[] convention (same as TFI/DMP).

FormatInfo info();

/// Parse a 43-byte VGI file.  Returns a single Patch.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a Patch to the 43-byte VGI format.  Fields the format
/// cannot represent (name, pan, LFO enable/frequency) are silently
/// dropped, matching the convention of other serializers in this
/// library.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::vgi
