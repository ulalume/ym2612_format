#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

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

// ---- Macro types ----

/// Playback mode of a macro sequence.
enum class MacroType : uint8_t {
  Sequence = 0, ///< Step-by-step value sequence
  ADSR = 1,     ///< Envelope generator
  LFO = 2,      ///< Low-frequency oscillator
};

/// A single macro: a time-varying parameter sequence.
struct Macro {
  std::vector<int32_t> values; ///< Sequence values
  MacroType type = MacroType::Sequence;
  uint8_t loop = 255;    ///< Loop position (255 = no loop)
  uint8_t release = 255; ///< Release position (255 = no release)
  uint8_t speed = 1;     ///< Ticks per step
  uint8_t delay = 0;     ///< Ticks before macro starts

  bool empty() const { return values.empty(); }
  bool operator==(const Macro &) const = default;
};

/// Channel-level macros (FINS MA block codes 0-14).
struct ChannelMacros {
  Macro volume;      ///< Code 0
  Macro arpeggio;    ///< Code 1
  Macro duty;        ///< Code 2
  Macro wave;        ///< Code 3
  Macro pitch;       ///< Code 4
  Macro ex1;         ///< Code 5
  Macro ex2;         ///< Code 6
  Macro ex3;         ///< Code 7
  Macro algorithm;   ///< Code 8
  Macro feedback;    ///< Code 9
  Macro fms;         ///< Code 10
  Macro ams;         ///< Code 11
  Macro pan_left;    ///< Code 12
  Macro pan_right;   ///< Code 13
  Macro phase_reset; ///< Code 14

  bool empty() const {
    return volume.empty() && arpeggio.empty() && duty.empty() &&
           wave.empty() && pitch.empty() && ex1.empty() && ex2.empty() &&
           ex3.empty() && algorithm.empty() && feedback.empty() &&
           fms.empty() && ams.empty() && pan_left.empty() &&
           pan_right.empty() && phase_reset.empty();
  }

  bool operator==(const ChannelMacros &) const = default;
};

/// Per-operator macros (FINS O1-O4 block codes 0-10).
struct OperatorMacros {
  Macro tl;  ///< Code 0
  Macro ar;  ///< Code 1
  Macro dr;  ///< Code 2
  Macro d2r; ///< Code 3
  Macro rr;  ///< Code 4
  Macro sl;  ///< Code 5
  Macro dt;  ///< Code 6
  Macro ml;  ///< Code 7
  Macro rs;  ///< Code 8
  Macro ssg; ///< Code 9
  Macro am;  ///< Code 10

  bool empty() const {
    return tl.empty() && ar.empty() && dr.empty() && d2r.empty() &&
           rr.empty() && sl.empty() && dt.empty() && ml.empty() &&
           rs.empty() && ssg.empty() && am.empty();
  }

  bool operator==(const OperatorMacros &) const = default;
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

  // Instrument (static parameters)
  uint8_t algorithm = 0; ///< Algorithm (0-7)
  uint8_t feedback = 0;  ///< Feedback (0-7)
  std::array<Operator, 4> operators{};

  // Macro data (empty by default — formats that lack macros leave these empty)
  ChannelMacros macros;
  std::array<OperatorMacros, 4> operator_macros{};

  /// True if any macro data is present.
  bool has_macros() const {
    if (!macros.empty())
      return true;
    for (const auto &om : operator_macros)
      if (!om.empty())
        return true;
    return false;
  }

  bool operator==(const Patch &) const = default;
};

} // namespace ym2612_format
