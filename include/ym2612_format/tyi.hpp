#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::tyi {

/// TYI FM instrument format — 32 bytes, one instrument per file.  A raw
/// YM2612 register dump grouped by parameter, closed by an ASCII "YI"
/// signature.
///
/// Values are stored exactly as written to the chip, so Detune keeps its
/// full hardware encoding (0-7) and round-trips losslessly — unlike the
/// linear encoding used by TFI/VGI.  Unlike EIF the format also carries
/// channel FMS/AMS.  Fields it does NOT represent and therefore silently
/// drop on write:
///
///   - patch name (use the filename to reconstruct)
///   - L/R panning, DAC enable
///   - LFO enable/frequency
///
/// Byte layout (operators in register-slot order, matching this
/// library's Patch::operators[] convention):
///
///   0x00-0x03: MUL | DT << 4, one byte per op     (register $30+)
///   0x04-0x07: TL                                 (register $40+)
///   0x08-0x0B: AR | RS << 6                       (register $50+)
///   0x0C-0x0F: DR | AM-EN << 7                    (register $60+)
///   0x10-0x13: SR                                 (register $70+)
///   0x14-0x17: RR | SL << 4                       (register $80+)
///   0x18-0x1B: SSG-EG (bit3 = enable, bits 0-2 = mode)  (register $90+)
///   0x1C:      Algorithm | Feedback << 3          (register $B0)
///   0x1D:      FMS | AMS << 4                     (register $B4)
///   0x1E-0x1F: ASCII "YI"
///
/// Bits 6-7 of 0x1D are the $B4 L/R panning bits.  Producers leave them
/// clear, which on hardware would mute the channel, so they are ignored
/// on read and written as 0.

FormatInfo info();

/// Parse a 32-byte TYI file.  Returns a single Patch.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a Patch to the 32-byte TYI format.  Fields the format
/// cannot represent (name, pan, LFO, DAC) are silently dropped,
/// matching the convention of other serializers in this library.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::tyi
