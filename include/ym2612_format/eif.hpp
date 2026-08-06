#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::eif {

/// Echo sound engine .eif FM instrument format — 29 bytes, one
/// instrument per file.  A raw YM2612 register dump grouped by
/// parameter (see github.com/sikthehedgehog/Echo).
///
/// Because values are stored exactly as written to the chip, Detune
/// keeps its full hardware encoding (0-7) and round-trips losslessly —
/// unlike the linear encoding used by TFI/VGI.  Fields the format does
/// NOT represent and therefore silently drop on write:
///
///   - patch name (use the filename to reconstruct)
///   - channel FMS/AMS (Echo sets $B4 itself)
///   - L/R panning, DAC enable
///   - LFO enable/frequency
///
/// Byte layout (operators in register-slot order, matching this
/// library's Patch::operators[] convention):
///
///   0x00:      Algorithm | Feedback << 3          (register $B0)
///   0x01-0x04: MUL | DT << 4, one byte per op     (register $30+)
///   0x05-0x08: TL                                 (register $40+)
///   0x09-0x0C: AR | RS << 6                       (register $50+)
///   0x0D-0x10: DR | AM-EN << 7                    (register $60+)
///   0x11-0x14: SR                                 (register $70+)
///   0x15-0x18: RR | SL << 4                       (register $80+)
///   0x19-0x1C: SSG-EG (bit3 = enable, bits 0-2 = mode)  (register $90+)

FormatInfo info();

/// Parse a 29-byte EIF file.  Returns a single Patch.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a Patch to the 29-byte EIF format.  Fields the format
/// cannot represent (name, FMS/AMS, pan, LFO) are silently dropped,
/// matching the convention of other serializers in this library.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::eif
