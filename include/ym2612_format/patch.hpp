#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace ym2612_format {

/// YM2612 operator settings (one of four per instrument).
struct Operator {
  uint8_t ar = 0;  ///< Attack rate (0-31)
  uint8_t dr = 0;  ///< Decay rate / D1R (0-31)
  uint8_t sr = 0;  ///< Sustain rate / D2R (0-31)
  uint8_t rr = 0;  ///< Release rate (0-15)
  uint8_t sl = 0;  ///< Sustain level / D1L (0-15)
  uint8_t tl = 0;  ///< Total level (0-127)
  uint8_t ks = 0;  ///< Key scale / RS (0-3)
  uint8_t ml = 0;  ///< Multiple (0-15)
  uint8_t dt = 0;  ///< Detune — hardware register encoding (0-7)
  uint8_t ssg = 0; ///< SSG-EG waveform (0-7)
  bool ssg_enable = false;
  bool am = false;     ///< Amplitude modulation enable
  bool enable = true;  ///< Operator key-on enable

  bool operator==(const Operator &) const = default;
};

/// A complete YM2612 FM instrument patch.
struct Patch {
  std::string name;

  // Global settings
  bool dac_enable = false;
  bool lfo_enable = false;
  uint8_t lfo_frequency = 0; ///< LFO speed (0-7)

  // Channel settings
  bool left = true;
  bool right = true;
  uint8_t ams = 0; ///< AM sensitivity (0-3)
  uint8_t fms = 0; ///< FM sensitivity / PMS (0-7)

  // Instrument
  uint8_t algorithm = 0; ///< Algorithm (0-7)
  uint8_t feedback = 0;  ///< Feedback (0-7)
  std::array<Operator, 4> operators{};

  bool operator==(const Patch &) const = default;
};

} // namespace ym2612_format
