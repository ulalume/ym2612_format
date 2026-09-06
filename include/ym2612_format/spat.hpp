#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::spat {

/// Sona sound driver .spat FM instrument format ("SonaPatch") — 32
/// bytes, one instrument per file.  Spec: Sona sound driver 0.50,
/// doc/format-fm.txt (Javier Degirolmo).
///
/// Bytes 0x00-0x1C are byte-for-byte identical to this library's Echo
/// .eif layout — see eif.hpp for the full register table.  Bytes
/// 0x1D-0x1F are reserved and must be 0.
///
/// As with EIF, fields the format does NOT represent and therefore
/// silently drop on write:
///
///   - patch name (use the filename to reconstruct)
///   - channel FMS/AMS
///   - L/R panning, DAC enable
///   - LFO enable/frequency

FormatInfo info();

/// Parse a 32-byte SonaPatch file.  Returns a single Patch.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

/// Serialize a Patch to the 32-byte SonaPatch format.  Fields the
/// format cannot represent (name, FMS/AMS, pan, LFO) are silently
/// dropped, matching the convention of other serializers in this
/// library.
SerializeResult serialize(const Patch &patch);

} // namespace ym2612_format::spat
