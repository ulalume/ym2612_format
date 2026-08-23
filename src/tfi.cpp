#include "ym2612_format/tfi.hpp"

#include "ym2612_format/detune.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace ym2612_format::tfi {

namespace {

constexpr size_t kFileSize = 42;
constexpr size_t kHeaderSize = 2;
constexpr size_t kOpBytes = 10;

/// Sniff: TFI has no magic, so we validate every byte against its
/// documented range.  The spec documents Detune as 0-6 and SSG-EG as 0
/// or 8-15, but real files in the wild use the full underlying register
/// width (Detune 0-7, SSG-EG 0-15) — probably because the tracker dumps
/// the raw YM2203 register bits.  Accept the hardware range, not the
/// narrower spec range, or we reject real TFI files.  Every other field
/// is strictly range-checked, which is still tight enough to keep random
/// 42-byte binaries from passing.
bool looks_like_tfi(const uint8_t *data, size_t size) {
  if (size != kFileSize)
    return false;
  if (data[0] > 7 || data[1] > 7)
    return false;
  for (int op = 0; op < 4; ++op) {
    const uint8_t *p = data + kHeaderSize + op * kOpBytes;
    if (p[0] > 0x0F) return false; // MUL
    if (p[1] > 0x07) return false; // Detune (hw range 0-7; spec is 0-6)
    if (p[2] > 0x7F) return false; // TL
    if (p[3] > 0x03) return false; // RS
    if (p[4] > 0x1F) return false; // AR
    if (p[5] > 0x1F) return false; // DR
    if (p[6] > 0x1F) return false; // SR
    if (p[7] > 0x0F) return false; // RR
    if (p[8] > 0x0F) return false; // SL
    if (p[9] > 0x0F) return false; // SSG-EG (low nibble; bit3 = enable)
  }
  return true;
}

/// Human-readable list of the fields looks_like_tfi() rejected in a
/// 42-byte buffer, e.g. "OP1 MUL=31, OP4 MUL=127".
std::string out_of_range_summary(const uint8_t *data) {
  static constexpr struct {
    const char *name;
    uint8_t max;
  } kOpFields[kOpBytes] = {
      {"MUL", 0x0F}, {"DT", 0x07}, {"TL", 0x7F}, {"RS", 0x03},
      {"AR", 0x1F},  {"DR", 0x1F}, {"SR", 0x1F}, {"RR", 0x0F},
      {"SL", 0x0F},  {"SSG-EG", 0x0F},
  };
  std::string out;
  auto add = [&out](const std::string &item) {
    if (!out.empty())
      out += ", ";
    out += item;
  };
  if (data[0] > 7)
    add("ALG=" + std::to_string(data[0]));
  if (data[1] > 7)
    add("FB=" + std::to_string(data[1]));
  for (int op = 0; op < 4; ++op) {
    const uint8_t *p = data + kHeaderSize + op * kOpBytes;
    for (size_t i = 0; i < kOpBytes; ++i) {
      if (p[i] > kOpFields[i].max)
        add("OP" + std::to_string(op + 1) + " " + kOpFields[i].name + "=" +
            std::to_string(p[i]));
    }
  }
  return out;
}

} // namespace

FormatInfo info() {
  return {Format::Tfi, "TFM Music Maker", "tfi", true, true, false};
}

static ParseResult parse_impl(const uint8_t *data, size_t size,
                              const std::string &fallback_name,
                              bool allow_out_of_range) {
  if (!data || size == 0)
    return Error{"Empty data"};

  std::vector<std::string> warnings;
  if (!looks_like_tfi(data, size)) {
    // The compatibility path forgives field ranges only — the exact
    // 42-byte layout is required either way, since a wrong size means
    // the data is foreign rather than a sloppily written TFI.
    if (!allow_out_of_range || size != kFileSize)
      return Error{"Not a TFI file (expected 42 bytes with valid ranges)"};
    warnings.push_back("TFI fields out of range (" +
                       out_of_range_summary(data) +
                       "), masked to hardware ranges");
  }

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
  patch.feedback = data[1] & 0x07;

  for (int op = 0; op < 4; ++op) {
    auto &o = patch.operators[op];
    const uint8_t *p = data + kHeaderSize + op * kOpBytes;

    o.ml = p[0] & 0x0F;
    o.dt = detune_from_linear(p[1]);
    o.tl = p[2] & 0x7F;
    o.ks = p[3] & 0x03;
    o.ar = p[4] & 0x1F;
    o.dr = p[5] & 0x1F;
    o.sr = p[6] & 0x1F;
    o.rr = p[7] & 0x0F;
    o.sl = p[8] & 0x0F;

    uint8_t ssg = p[9];
    o.ssg_enable = (ssg & 0x08) != 0;
    o.ssg = ssg & 0x07;

    // TFI has no per-op AM-EN field; leave at default (false).
    o.am = false;
    o.enable = true;
  }

  return ParseOk{{std::move(patch)}, std::move(warnings)};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  return parse_impl(data, size, fallback_name, false);
}

ParseResult parse_compatible(const uint8_t *data, size_t size,
                             const std::string &fallback_name) {
  return parse_impl(data, size, fallback_name, true);
}

SerializeResult serialize(const Patch &patch) {
  std::vector<uint8_t> data(kFileSize, 0);

  data[0] = patch.algorithm & 0x07;
  data[1] = patch.feedback & 0x07;

  for (int op = 0; op < 4; ++op) {
    const auto &o = patch.operators[op];
    uint8_t *p = data.data() + kHeaderSize + op * kOpBytes;

    p[0] = o.ml & 0x0F;
    p[1] = detune_to_linear(o.dt & 0x07);
    p[2] = std::min<uint8_t>(o.tl, 127);
    p[3] = o.ks & 0x03;
    p[4] = std::min<uint8_t>(o.ar, 31);
    p[5] = std::min<uint8_t>(o.dr, 31);
    p[6] = std::min<uint8_t>(o.sr, 31);
    p[7] = std::min<uint8_t>(o.rr, 15);
    p[8] = std::min<uint8_t>(o.sl, 15);
    p[9] = (o.ssg_enable ? 0x08 : 0x00) | (o.ssg & 0x07);
  }

  return data;
}

} // namespace ym2612_format::tfi
