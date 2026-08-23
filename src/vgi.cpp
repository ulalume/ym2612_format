#include "ym2612_format/vgi.hpp"

#include "ym2612_format/detune.hpp"

#include <algorithm>
#include <vector>

namespace ym2612_format::vgi {

namespace {

constexpr size_t kFileSize = 43;
constexpr size_t kHeaderSize = 3;
constexpr size_t kOpBytes = 10;

/// Sniff: VGI has no magic, so we validate every byte against its
/// documented range (same approach as TFI).  The spec documents Detune
/// as 0-6, but we accept the full hardware width 0-7 to match real
/// files in the wild, mirroring the TFI parser.  Bit 7 of the DR byte
/// is the AM-EN flag; bits 5-6 must be clear.
bool looks_like_vgi(const uint8_t *data, size_t size) {
  if (size != kFileSize)
    return false;
  if (data[0] > 7 || data[1] > 7)
    return false;
  if (data[2] & 0xC8) // bits 0-2 FMS, bits 4-5 AMS; rest must be clear
    return false;
  for (int op = 0; op < 4; ++op) {
    const uint8_t *p = data + kHeaderSize + op * kOpBytes;
    if (p[0] > 0x0F) return false; // MUL
    if (p[1] > 0x07) return false; // Detune (hw range 0-7; spec is 0-6)
    if (p[2] > 0x7F) return false; // TL
    if (p[3] > 0x03) return false; // RS
    if (p[4] > 0x1F) return false; // AR
    if (p[5] & 0x60) return false; // DR (bit7 = AM-EN)
    if (p[6] > 0x1F) return false; // SR
    if (p[7] > 0x0F) return false; // RR
    if (p[8] > 0x0F) return false; // SL
    if (p[9] > 0x0F) return false; // SSG-EG (bit3 = enable)
  }
  return true;
}

} // namespace

FormatInfo info() {
  return {Format::Vgi, "VGM Music Maker", "vgi", true, true, false};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};
  if (!looks_like_vgi(data, size))
    return Error{"Not a VGI file (expected 43 bytes with valid ranges)"};

  Patch patch;
  patch.name = fallback_name;
  patch.dac_enable = false;
  patch.lfo_enable = false;
  patch.lfo_frequency = 0;
  patch.left = true;
  patch.right = true;
  patch.algorithm = data[0] & 0x07;
  patch.feedback = data[1] & 0x07;
  patch.fms = data[2] & 0x07;
  patch.ams = (data[2] >> 4) & 0x03;

  for (int op = 0; op < 4; ++op) {
    auto &o = patch.operators[op];
    const uint8_t *p = data + kHeaderSize + op * kOpBytes;

    o.ml = p[0] & 0x0F;
    o.dt = detune_from_linear(p[1]);
    o.tl = p[2] & 0x7F;
    o.ks = p[3] & 0x03;
    o.ar = p[4] & 0x1F;
    o.dr = p[5] & 0x1F;
    o.am = (p[5] & 0x80) != 0;
    o.sr = p[6] & 0x1F;
    o.rr = p[7] & 0x0F;
    o.sl = p[8] & 0x0F;

    uint8_t ssg = p[9];
    o.ssg_enable = (ssg & 0x08) != 0;
    o.ssg = ssg & 0x07;

    o.enable = true;
  }

  return ParseOk{{std::move(patch)}, {}};
}

SerializeResult serialize(const Patch &patch) {
  std::vector<uint8_t> data(kFileSize, 0);

  data[0] = patch.algorithm & 0x07;
  data[1] = patch.feedback & 0x07;
  data[2] = (patch.fms & 0x07) | ((patch.ams & 0x03) << 4);

  for (int op = 0; op < 4; ++op) {
    const auto &o = patch.operators[op];
    uint8_t *p = data.data() + kHeaderSize + op * kOpBytes;

    p[0] = o.ml & 0x0F;
    p[1] = detune_to_linear(o.dt & 0x07);
    p[2] = std::min<uint8_t>(o.tl, 127);
    p[3] = o.ks & 0x03;
    p[4] = std::min<uint8_t>(o.ar, 31);
    p[5] = std::min<uint8_t>(o.dr, 31) | (o.am ? 0x80 : 0x00);
    p[6] = std::min<uint8_t>(o.sr, 31);
    p[7] = std::min<uint8_t>(o.rr, 15);
    p[8] = std::min<uint8_t>(o.sl, 15);
    p[9] = (o.ssg_enable ? 0x08 : 0x00) | (o.ssg & 0x07);
  }

  return data;
}

} // namespace ym2612_format::vgi
