#include "ym2612_format/eif.hpp"

#include <algorithm>
#include <vector>

namespace ym2612_format::eif {

namespace {

constexpr size_t kFileSize = 29;

/// Sniff: EIF has no magic, so we validate the bits that are unused in
/// the underlying registers.  Weaker than the TFI/VGI checks (several
/// registers use all 8 bits), but combined with the fixed 29-byte size
/// it still rejects most non-EIF data.
bool looks_like_eif(const uint8_t *data, size_t size) {
  if (size != kFileSize)
    return false;
  if (data[0] & 0xC0) // $B0: algorithm 3 bits + feedback 3 bits
    return false;
  for (int op = 0; op < 4; ++op) {
    if (data[1 + op] & 0x80)  return false; // $30: MUL | DT<<4, bit7 unused
    if (data[5 + op] > 0x7F)  return false; // $40: TL is 7 bits
    // $50 (AR | RS<<6) and $80 (RR | SL<<4) use all 8 bits — no check.
    if (data[13 + op] & 0x60) return false; // $60: DR | AM<<7, bits 5-6 unused
    if (data[17 + op] > 0x1F) return false; // $70: SR is 5 bits
    if (data[25 + op] > 0x0F) return false; // $90: SSG-EG is 4 bits
  }
  return true;
}

} // namespace

FormatInfo info() {
  return {Format::Eif, "Echo (EIF)", "eif", true, true, false};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};
  if (!looks_like_eif(data, size))
    return Error{"Not an EIF file (expected 29 bytes with valid ranges)"};

  Patch patch;
  patch.name = fallback_name;
  patch.dac_enable = false;
  patch.lfo_enable = false;
  patch.lfo_frequency = 0;
  patch.left = true;
  patch.right = true;
  patch.ams = 0;
  patch.fms = 0;
  patch.algorithm = data[0] & 0x07;
  patch.feedback = (data[0] >> 3) & 0x07;

  for (int op = 0; op < 4; ++op) {
    auto &o = patch.operators[op];

    o.ml = data[1 + op] & 0x0F;
    o.dt = (data[1 + op] >> 4) & 0x07; // hardware encoding, kept as-is
    o.tl = data[5 + op] & 0x7F;
    o.ar = data[9 + op] & 0x1F;
    o.ks = (data[9 + op] >> 6) & 0x03;
    o.dr = data[13 + op] & 0x1F;
    o.am = (data[13 + op] & 0x80) != 0;
    o.sr = data[17 + op] & 0x1F;
    o.rr = data[21 + op] & 0x0F;
    o.sl = (data[21 + op] >> 4) & 0x0F;

    uint8_t ssg = data[25 + op];
    o.ssg_enable = (ssg & 0x08) != 0;
    o.ssg = ssg & 0x07;

    o.enable = true;
  }

  return ParseOk{{std::move(patch)}, {}};
}

SerializeResult serialize(const Patch &patch) {
  std::vector<uint8_t> data(kFileSize, 0);

  data[0] = (patch.algorithm & 0x07) | ((patch.feedback & 0x07) << 3);

  for (int op = 0; op < 4; ++op) {
    const auto &o = patch.operators[op];

    data[1 + op] = (o.ml & 0x0F) | ((o.dt & 0x07) << 4);
    data[5 + op] = std::min<uint8_t>(o.tl, 127);
    data[9 + op] = std::min<uint8_t>(o.ar, 31) | ((o.ks & 0x03) << 6);
    data[13 + op] = std::min<uint8_t>(o.dr, 31) | (o.am ? 0x80 : 0x00);
    data[17 + op] = std::min<uint8_t>(o.sr, 31);
    data[21 + op] = std::min<uint8_t>(o.rr, 15) | ((o.sl & 0x0F) << 4);
    data[25 + op] = (o.ssg_enable ? 0x08 : 0x00) | (o.ssg & 0x07);
  }

  return data;
}

} // namespace ym2612_format::eif
