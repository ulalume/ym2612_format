#pragma once

#include <cstdint>

namespace ym2612_format {

/// Convert DMP/FUI linear detune (0=-3 … 3=0 … 6=+3) to hardware register
/// encoding (0-7).
///
/// Hardware register layout (3 bits of $30+):
///   0 = 0,  1 = +1,  2 = +2,  3 = +3
///   4 = 0,  5 = -1,  6 = -2,  7 = -3
inline uint8_t detune_from_linear(int dt) {
  switch (dt) {
  case 0:
    return 7; // -3
  case 1:
    return 6; // -2
  case 2:
    return 5; // -1
  case 3:
    return 4; // 0  (could also be 0; default to 4)
  case 4:
    return 1; // +1
  case 5:
    return 2; // +2
  case 6:
  case 7:
    return 3; // +3
  default:
    return 4;
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
