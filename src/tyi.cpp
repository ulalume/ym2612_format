#include "ym2612_format/tyi.hpp"

#include <algorithm>
#include <vector>

namespace ym2612_format::tyi {

namespace {

constexpr size_t kFileSize = 32;

/// Sniff: beyond the fixed size and the "YI" signature, validate the
/// bits that are unused in the underlying registers.  Byte 0x1D is
/// exempt — its top two bits are the $B4 panning flags.
bool ranges_valid(const uint8_t *data) {
  if (data[28] & 0xC0) // $B0: algorithm 3 bits + feedback 3 bits
    return false;
  for (int op = 0; op < 4; ++op) {
    if (data[0 + op] & 0x80)  return false; // $30: MUL | DT<<4, bit7 unused
    if (data[4 + op] & 0x80)  return false; // $40: TL is 7 bits
    if (data[8 + op] & 0x20)  return false; // $50: AR | RS<<6, bit5 unused
    if (data[12 + op] & 0x60) return false; // $60: DR | AM<<7, bits 5-6 unused
    if (data[16 + op] & 0xE0) return false; // $70: SR is 5 bits
    // $80 (RR | SL<<4) uses all 8 bits — nothing to check.
    if (data[24 + op] & 0xF0) return false; // $90: SSG-EG is 4 bits
  }
  return true;
}

} // namespace

FormatInfo info() {
  return {Format::Tyi, "TYI", "tyi", true, true, false};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};
  if (size != kFileSize)
    return Error{"Not a TYI file (expected 32 bytes)"};
  if (data[30] != 'Y' || data[31] != 'I')
    return Error{"Not a TYI file (missing YI signature)"};
  if (!ranges_valid(data))
    return Error{"Not a TYI file (invalid register ranges)"};

  Patch patch;
  patch.name = fallback_name;
  patch.dac_enable = false;
  patch.lfo_enable = false;
  patch.lfo_frequency = 0;
  // Bits 6-7 of $B4 are the panning flags; producers leave them clear,
  // which would otherwise read back as a silent channel.
  patch.left = true;
  patch.right = true;
  patch.ams = (data[29] >> 4) & 0x03;
  patch.fms = data[29] & 0x07;
  patch.algorithm = data[28] & 0x07;
  patch.feedback = (data[28] >> 3) & 0x07;

  for (int op = 0; op < 4; ++op) {
    auto &o = patch.operators[op];

    o.ml = data[0 + op] & 0x0F;
    o.dt = (data[0 + op] >> 4) & 0x07; // hardware encoding, kept as-is
    o.tl = data[4 + op] & 0x7F;
    o.ar = data[8 + op] & 0x1F;
    o.ks = (data[8 + op] >> 6) & 0x03;
    o.dr = data[12 + op] & 0x1F;
    o.am = (data[12 + op] & 0x80) != 0;
    o.sr = data[16 + op] & 0x1F;
    o.rr = data[20 + op] & 0x0F;
    o.sl = (data[20 + op] >> 4) & 0x0F;

    uint8_t ssg = data[24 + op];
    o.ssg_enable = (ssg & 0x08) != 0;
    o.ssg = ssg & 0x07;

    o.enable = true;
  }

  return ParseOk{{std::move(patch)}, {}};
}

SerializeResult serialize(const Patch &patch) {
  std::vector<uint8_t> data(kFileSize, 0);

  for (int op = 0; op < 4; ++op) {
    const auto &o = patch.operators[op];

    data[0 + op] = (o.ml & 0x0F) | ((o.dt & 0x07) << 4);
    data[4 + op] = std::min<uint8_t>(o.tl, 127);
    data[8 + op] = std::min<uint8_t>(o.ar, 31) | ((o.ks & 0x03) << 6);
    data[12 + op] = std::min<uint8_t>(o.dr, 31) | (o.am ? 0x80 : 0x00);
    data[16 + op] = std::min<uint8_t>(o.sr, 31);
    data[20 + op] = std::min<uint8_t>(o.rr, 15) | ((o.sl & 0x0F) << 4);
    data[24 + op] = (o.ssg_enable ? 0x08 : 0x00) | (o.ssg & 0x07);
  }

  data[28] = (patch.algorithm & 0x07) | ((patch.feedback & 0x07) << 3);
  data[29] = (patch.fms & 0x07) | ((patch.ams & 0x03) << 4); // pan bits stay 0
  data[30] = 'Y';
  data[31] = 'I';

  return data;
}

} // namespace ym2612_format::tyi
