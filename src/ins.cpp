#include "ym2612_format/ins.hpp"

#include <cstring>

namespace ym2612_format::ins {

namespace {

constexpr size_t kSignatureSize = 5;
constexpr size_t kBodySize = 25;
/// Signature + shortest possible (empty) name + patch data.
constexpr size_t kMinSize = kSignatureSize + 1 + kBodySize;

/// Sniff: beyond the signature, validate the bits that are unused in
/// the underlying registers.
bool ranges_valid(const uint8_t *body) {
  if (body[24] & 0xC0) // $B0: algorithm 3 bits + feedback 3 bits
    return false;
  for (int op = 0; op < 4; ++op) {
    if (body[0 + op] & 0x80)  return false; // $30: MUL | DT<<4, bit7 unused
    if (body[4 + op] & 0x80)  return false; // $40: TL is 7 bits
    if (body[8 + op] & 0x20)  return false; // $50: AR | RS<<6, bit5 unused
    if (body[12 + op] & 0x60) return false; // $60: DR | AM<<7, bits 5-6 unused
    if (body[16 + op] & 0xE0) return false; // $70: SR is 5 bits
    // $80 (RR | SL<<4) uses all 8 bits — nothing to check.
  }
  return true;
}

} // namespace

FormatInfo info() {
  return {Format::Ins, "MVS Tracker MD", "ins", true, false, false};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};
  if (size < kMinSize)
    return Error{"Not an INS file (too small)"};
  if (std::memcmp(data, "MVSI1", kSignatureSize) != 0)
    return Error{"Not an INS file (missing MVSI1 signature)"};

  size_t terminator = kSignatureSize;
  while (terminator < size && data[terminator] != 0)
    ++terminator;
  if (terminator == size)
    return Error{"Not an INS file (unterminated name)"};

  size_t body_offset = terminator + 1;
  if (size - body_offset != kBodySize)
    return Error{"Not an INS file (expected 25 bytes of patch data)"};

  const uint8_t *body = data + body_offset;
  if (!ranges_valid(body))
    return Error{"Not an INS file (invalid register ranges)"};

  std::string name(reinterpret_cast<const char *>(data + kSignatureSize),
                   terminator - kSignatureSize);

  Patch patch;
  patch.name = name.empty() ? fallback_name : name;
  patch.dac_enable = false;
  patch.lfo_enable = false;
  patch.lfo_frequency = 0;
  patch.left = true;
  patch.right = true;
  patch.ams = 0;
  patch.fms = 0;
  patch.algorithm = body[24] & 0x07;
  patch.feedback = (body[24] >> 3) & 0x07;

  for (int op = 0; op < 4; ++op) {
    auto &o = patch.operators[op];

    o.ml = body[0 + op] & 0x0F;
    o.dt = (body[0 + op] >> 4) & 0x07; // hardware encoding, kept as-is
    o.tl = body[4 + op] & 0x7F;
    o.ar = body[8 + op] & 0x1F;
    o.ks = (body[8 + op] >> 6) & 0x03;
    o.dr = body[12 + op] & 0x1F;
    o.am = (body[12 + op] & 0x80) != 0;
    o.sr = body[16 + op] & 0x1F;
    o.rr = body[20 + op] & 0x0F;
    o.sl = (body[20 + op] >> 4) & 0x0F;

    o.ssg_enable = false;
    o.ssg = 0;

    o.enable = true;
  }

  return ParseOk{{std::move(patch)}, {}};
}

} // namespace ym2612_format::ins
