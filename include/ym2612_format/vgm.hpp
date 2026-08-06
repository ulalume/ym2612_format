#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::vgm {

/// VGM / VGZ (Video Game Music) register-log import — read-only,
/// extracts YM2612 FM instruments.
///
/// VGM files don't contain an instrument list; they are logs of chip
/// register writes.  Patches are reconstructed from the chip state
/// using the approach of vgm2pre / shiru's VGM2TFI:
///
///   1. Every YM2612 write (commands 0x52/0x53) updates a shadow copy
///      of the six channels' patch registers ($30-$9F operator
///      parameters, $B0-$B2 algorithm/feedback, $B4-$B6 FMS/AMS).
///   2. A key-on ($28) snapshots the channel state — an instrument is
///      "whatever the channel registers held when a note was struck".
///      Channel 6 is skipped while the DAC is enabled.
///   3. Silent states (all operators AR=0, or all TL=127) are ignored.
///   4. Snapshots that differ only in carrier-operator TL (i.e. the
///      driver's volume control) are grouped as one instrument and the
///      loudest variant is kept.  FMS/AMS, per-op AM-EN and the LFO
///      state are also excluded from this identity comparison, since
///      they change with musical expression.
///
/// The global LFO register ($22) is captured at key-on into
/// lfo_enable / lfo_frequency.  Panning ($B4 bits 6-7) is performance
/// data and is normalized to L=R=on.
///
/// Limitations inherent to the source format:
///
///   - Only instruments actually played appear in the output.
///   - Carrier TL reflects the loudest volume heard in the song.
///   - Software envelopes/effects performed via register rewrites are
///     not reconstructed (no macros are generated).
///   - Note frequencies ($A0-$AF) and DAC samples are discarded.
///   - Only the first YM2612 of a dual-chip VGM is scanned.
///
/// Patches are named "<name>_<index>" in order of first appearance.
/// VGZ (gzip-compressed VGM) is decompressed transparently.

FormatInfo info();

/// Parse a VGM or VGZ file.  Returns zero or more Patches; a valid
/// VGM with no YM2612 key-on events yields an empty patch list plus a
/// warning.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

} // namespace ym2612_format::vgm
