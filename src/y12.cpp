#include "ym2612_format/y12.hpp"

#include <algorithm>
#include <vector>

namespace ym2612_format::y12 {

namespace {

constexpr size_t kFileSize = 128;
constexpr size_t kBlockSize = 16;
constexpr size_t kBlockUsed = 7; ///< Bytes used in an operator block
constexpr size_t kAlgorithm = 0x40;
constexpr size_t kFeedback = 0x41;
constexpr size_t kStrings = 0x50; ///< Start of the three ROM-name strings

/// Sniff: Y12 has no magic, so validate the bits that are unused in the
/// underlying registers plus the reserved bytes, which the format keeps
/// zero.  The trailing strings hold arbitrary text and are not checked.
bool looks_like_y12(const uint8_t *data) {
  for (int op = 0; op < 4; ++op) {
    const uint8_t *b = data + op * kBlockSize;
    if (b[0] & 0x80) return false; // $30: MUL | DT<<4, bit7 unused
    if (b[1] & 0x80) return false; // $40: TL is 7 bits
    if (b[2] & 0x20) return false; // $50: AR | RS<<6, bit5 unused
    if (b[3] & 0x60) return false; // $60: DR | AM<<7, bits 5-6 unused
    if (b[4] & 0xE0) return false; // $70: SR is 5 bits
    // $80 (RR | SL<<4) uses all 8 bits — nothing to check.
    if (b[6] & 0xF0) return false; // $90: SSG-EG is 4 bits
    for (size_t i = kBlockUsed; i < kBlockSize; ++i)
      if (b[i] != 0)
        return false;
  }
  if (data[kAlgorithm] > 7 || data[kFeedback] > 7)
    return false;
  for (size_t i = kFeedback + 1; i < kStrings; ++i)
    if (data[i] != 0)
      return false;
  return true;
}

} // namespace

FormatInfo info() {
  return {Format::Y12, "Gens KMod", "y12", true, true, false};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};
  if (size != kFileSize)
    return Error{"Not a Y12 file (expected 128 bytes)"};
  if (!looks_like_y12(data))
    return Error{"Not a Y12 file (invalid register ranges)"};

  Patch patch;
  patch.name = fallback_name;
  patch.dac_enable = false;
  patch.lfo_enable = false;
  patch.lfo_frequency = 0;
  patch.left = true;
  patch.right = true;
  patch.ams = 0;
  patch.fms = 0;
  patch.algorithm = data[kAlgorithm] & 0x07;
  patch.feedback = data[kFeedback] & 0x07;

  for (int op = 0; op < 4; ++op) {
    const uint8_t *b = data + op * kBlockSize;
    auto &o = patch.operators[op];

    o.ml = b[0] & 0x0F;
    o.dt = (b[0] >> 4) & 0x07; // hardware encoding, kept as-is
    o.tl = b[1] & 0x7F;
    o.ar = b[2] & 0x1F;
    o.ks = (b[2] >> 6) & 0x03;
    o.dr = b[3] & 0x1F;
    o.am = (b[3] & 0x80) != 0;
    o.sr = b[4] & 0x1F;
    o.rr = b[5] & 0x0F;
    o.sl = (b[5] >> 4) & 0x0F;

    o.ssg_enable = (b[6] & 0x08) != 0;
    o.ssg = b[6] & 0x07;

    o.enable = true;
  }

  return ParseOk{{std::move(patch)}, {}};
}

SerializeResult serialize(const Patch &patch) {
  std::vector<uint8_t> data(kFileSize, 0);

  for (int op = 0; op < 4; ++op) {
    uint8_t *b = data.data() + op * kBlockSize;
    const auto &o = patch.operators[op];

    b[0] = (o.ml & 0x0F) | ((o.dt & 0x07) << 4);
    b[1] = std::min<uint8_t>(o.tl, 127);
    b[2] = std::min<uint8_t>(o.ar, 31) | ((o.ks & 0x03) << 6);
    b[3] = std::min<uint8_t>(o.dr, 31) | (o.am ? 0x80 : 0x00);
    b[4] = std::min<uint8_t>(o.sr, 31);
    b[5] = std::min<uint8_t>(o.rr, 15) | ((o.sl & 0x0F) << 4);
    b[6] = (o.ssg_enable ? 0x08 : 0x00) | (o.ssg & 0x07);
  }

  data[kAlgorithm] = patch.algorithm & 0x07;
  data[kFeedback] = patch.feedback & 0x07;

  return data;
}

} // namespace ym2612_format::y12
