#include "ym2612_format/dat.hpp"

namespace ym2612_format::dat {

namespace {

constexpr size_t kHeaderSize = 2; ///< Big-endian pair count

/// Registers the format is allowed to carry: the four per-operator
/// registers of each $30-$9F group, plus the channel registers $B0
/// and $B4.
bool address_allowed(uint8_t reg) {
  if (reg >= 0x30 && reg <= 0x9F)
    return (reg & 0x03) == 0;
  return reg == 0xB0 || reg == 0xB4;
}

/// Sniff: the list has no magic, so validate the bits that are unused
/// in each register.
bool value_in_range(uint8_t reg, uint8_t value) {
  switch (reg & 0xF0) {
  case 0x30: return (value & 0x80) == 0; // MUL | DT<<4, bit7 unused
  case 0x40: return (value & 0x80) == 0; // TL is 7 bits
  case 0x50: return (value & 0x20) == 0; // AR | RS<<6, bit5 unused
  case 0x60: return (value & 0x60) == 0; // DR | AM<<7, bits 5-6 unused
  case 0x70: return (value & 0xE0) == 0; // SR is 5 bits
  case 0x80: return true;                // RR | SL<<4 uses all 8 bits
  case 0x90: return (value & 0xF0) == 0; // SSG-EG is 4 bits
  case 0xB0:
    // $B0 is algorithm 3 bits + feedback 3 bits; $B4 uses all 8 bits
    // (bits 6-7 are the panning flags).
    return reg != 0xB0 || (value & 0xC0) == 0;
  }
  return false;
}

} // namespace

FormatInfo info() {
  return {Format::Dat, "YM2612 Instrument Editor", "dat", true, false, false};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};
  if (size < 4)
    return Error{"Not a DAT file (too small)"};

  size_t count = (static_cast<size_t>(data[0]) << 8) | data[1];
  if (count == 0 || size != kHeaderSize + count * 2)
    return Error{"Not a DAT file (size does not match pair count)"};

  bool has_algorithm = false;
  int previous = -1;
  for (size_t i = 0; i < count; ++i) {
    uint8_t reg = data[kHeaderSize + i * 2];
    uint8_t value = data[kHeaderSize + i * 2 + 1];
    if (static_cast<int>(reg) <= previous || !address_allowed(reg) ||
        !value_in_range(reg, value))
      return Error{"Not a DAT file (invalid register list)"};
    previous = reg;
    if (reg == 0xB0)
      has_algorithm = true;
  }
  if (!has_algorithm)
    return Error{"Not a DAT file (invalid register list)"};

  // Registers absent from the list keep their Patch defaults.
  Patch patch;
  patch.name = fallback_name;
  patch.dac_enable = false;
  patch.lfo_enable = false;
  patch.lfo_frequency = 0;
  patch.left = true;
  patch.right = true;
  for (auto &o : patch.operators)
    o.enable = true;

  for (size_t i = 0; i < count; ++i) {
    uint8_t reg = data[kHeaderSize + i * 2];
    uint8_t value = data[kHeaderSize + i * 2 + 1];
    auto &o = patch.operators[(reg >> 2) & 0x03];

    switch (reg & 0xF0) {
    case 0x30:
      o.ml = value & 0x0F;
      o.dt = (value >> 4) & 0x07; // hardware encoding, kept as-is
      break;
    case 0x40:
      o.tl = value & 0x7F;
      break;
    case 0x50:
      o.ar = value & 0x1F;
      o.ks = (value >> 6) & 0x03;
      break;
    case 0x60:
      o.dr = value & 0x1F;
      o.am = (value & 0x80) != 0;
      break;
    case 0x70:
      o.sr = value & 0x1F;
      break;
    case 0x80:
      o.rr = value & 0x0F;
      o.sl = (value >> 4) & 0x0F;
      break;
    case 0x90:
      o.ssg_enable = (value & 0x08) != 0;
      o.ssg = value & 0x07;
      break;
    case 0xB0:
      if (reg == 0xB0) {
        patch.algorithm = value & 0x07;
        patch.feedback = (value >> 3) & 0x07;
      } else {
        patch.fms = value & 0x07; // $B4; the panning bits are ignored
        patch.ams = (value >> 4) & 0x03;
      }
      break;
    }
  }

  return ParseOk{{std::move(patch)}, {}};
}

} // namespace ym2612_format::dat
