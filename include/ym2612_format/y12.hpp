#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::y12 {

/// Gens KMod .y12 FM instrument format — 128 bytes, one instrument per
/// file.  A raw YM2612 channel dump captured from an emulated ROM:
/// four 16-byte operator blocks, then algorithm and feedback as separate
/// bytes, then three 16-byte strings.
///
/// Values are stored exactly as written to the chip, so Detune keeps its
/// full hardware encoding (0-7) and round-trips losslessly — unlike the
/// linear encoding used by TFI/VGI.  Fields the format does NOT
/// represent and therefore silently drop on write:
///
///   - patch name (use the filename to reconstruct)
///   - channel FMS/AMS
///   - L/R panning, DAC enable
///   - LFO enable/frequency
///
/// Byte layout (operators in register-slot order, matching this
/// library's Patch::operators[] convention):
///
///   0x00, 0x10, 0x20, 0x30: one 16-byte block per operator
///     +0x00: MUL | DT << 4                        (register $30+)
///     +0x01: TL                                   (register $40+)
///     +0x02: AR | RS << 6                         (register $50+)
///     +0x03: DR | AM-EN << 7                      (register $60+)
///     +0x04: SR                                   (register $70+)
///     +0x05: RR | SL << 4                         (register $80+)
///     +0x06: SSG-EG (bit3 = enable, bits 0-2 = mode)  (register $90+)
///     +0x07-+0x0F: reserved, 0
///   0x40:      Algorithm (0-7)                    (register $B0 bits 0-2)
///   0x41:      Feedback (0-7)                     (register $B0 bits 3-5)
///   0x42-0x4F: reserved, 0
///   0x50, 0x60, 0x70: three 16-byte strings
///
/// The three trailing strings all hold the name of the ROM the dump came
/// from, not a patch name; they are neither validated nor read, and are
/// written as zero.

FormatInfo info();

/// Parse a 128-byte Gens KMod file.  Returns a single Patch.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a Patch to the 128-byte Gens KMod format.  Fields the
/// format cannot represent (name, FMS/AMS, pan, LFO) are silently
/// dropped, matching the convention of other serializers in this
/// library.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::y12
