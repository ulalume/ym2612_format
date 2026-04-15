#pragma once

#include "format.hpp"
#include "result.hpp"
#include <cstdint>

namespace ym2612_format::opm {

/// VOPM / MiOPMdrv .opm text format — read-only.
///
/// Originally a YM2151 (OPM) instrument format produced by VOPM /
/// MiOPMdrv.  DT1, AR/D1R/D2R/RR/D1L/TL/KS/MUL and AMS-EN map 1:1 onto
/// the YM2612 operator registers.  OPM-only parameters are handled as
/// follows:
///
///   - LFO LFRQ: approximated to OPN2 lfo_frequency (0-255 → 0-7; the two
///     chips use different LFO clocks so absolute rates differ).
///   - LFO AMD / PMD: no OPN2 equivalent — discarded, but used at import
///     time to gate the per-channel AMS / PMS (see below).
///   - LFO WF, NFRQ: no OPN2 equivalent — discarded with a warning.
///   - CH AMS / PMS: preserved, but only if OPM's AMD / PMD are non-zero.
///     On OPM they're sensitivities multiplied by the global depth; on
///     OPN2 there is no global depth, so copying AMS when AMD=0 would
///     introduce AM that wasn't in the original patch.
///   - CH SLOT mask and NE: discarded with a warning.
///   - DT2: no OPN2 register — discarded with a warning.
///
/// Typical layout (one instrument):
///   //MiOPMdrv sound bank Paramer Ver2002.04.22
///   //LFO: LFRQ AMD PMD WF NFRQ
///   //@:[Num] [Name]
///   //CH: PAN  FL CON AMS PMS SLOT NE
///   //[OPname]: AR D1R D2R RR D1L TL KS MUL DT1 DT2 AMS-EN
///
///   @:0 Instrument 0
///   LFO: 0 0 0 0 0
///   CH:  64 5 0 0 0 120 0
///   M1:  31 0 0 5 2 0 0 15 3 0 0
///   C1:  31 17 16 8 2 17 0 15 3 0 0
///   M2:  31 31 12 8 1 8 0 15 3 0 0
///   C2:  29 18 20 10 3 16 0 15 3 0 0
///
/// Operator label → YM2612 slot mapping (Furnace convention):
///   M1 → operators[0], M2 → operators[1],
///   C1 → operators[2], C2 → operators[3].

FormatInfo info();

/// Parse OPM text data.  A single file may define many instruments.
ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &name = "");

} // namespace ym2612_format::opm
