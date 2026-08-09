#pragma once

#include <cstdint>

namespace ym2612_format {

/// Convert DMP/FUI linear detune (0=-3 … 3=0 … 6=+3) to hardware register
/// encoding (0-7).
///
/// Hardware register layout (3 bits of $30+):
///   0 = 0,  1 = +1,  2 = +2,  3 = +3
///   4 = 0,  5 = -1,  6 = -2,  7 = -3
///
/// Registers 0 and 4 are hardware-equivalent (both "zero detune"; 4 is
/// "-0"). Register 0 is the canonical encoding this function emits for
/// the zero point, so linear-format zero detune round-trips to the same
/// value native register formats already use.
inline uint8_t detune_from_linear(int dt) {
  switch (dt) {
  case 0:
    return 7; // -3
  case 1:
    return 6; // -2
  case 2:
    return 5; // -1
  case 3:
    return 0; // 0 (canonical; register 4 is an equivalent "-0" alias)
  case 4:
    return 1; // +1
  case 5:
    return 2; // +2
  case 6:
  case 7:
    return 3; // +3
  default:
    return 0; // out-of-range input maps to canonical zero detune
  }
}

/// Convert hardware register detune (0-7) to DMP/FUI linear encoding.
inline uint8_t detune_to_linear(int dt) {
  switch (dt) {
  case 7:
    return 0; // -3
  case 6:
    return 1; // -2
  case 5:
    return 2; // -1
  case 3:
    return 6; // +3
  case 2:
    return 5; // +2
  case 1:
    return 4; // +1
  case 0:
  case 4:
  default:
    return 3; // 0
  }
}

} // namespace ym2612_format
