#include "ym2612_format/vgm.hpp"

// The extraction algorithm (shadow register state, key-on snapshots,
// silent-state filtering, carrier-TL volume grouping) is ported from
// vgm2pre — https://github.com/vgmtool/vgm2pre
// Copyright (c) 2013 Alex Rosario, MIT license — itself based on
// Shiru's VGM2TFI.  This file is a reimplementation, not a verbatim
// copy.

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <miniz.h>

namespace ym2612_format::vgm {

namespace {

uint32_t rd32(const uint8_t *d) {
  return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8) |
         (static_cast<uint32_t>(d[2]) << 16) |
         (static_cast<uint32_t>(d[3]) << 24);
}

// ---- VGZ (gzip) decompression ----

/// Refuse to inflate anything claiming to be larger than this.  Real
/// VGMs decompress to a few tens of MB at most; the cap keeps a
/// crafted decompression bomb from exhausting memory (which on the
/// wasm32 build would abort the whole module).
constexpr uint32_t kMaxInflatedSize = 256u << 20; // 256 MB

bool is_gzip(const uint8_t *data, size_t size) {
  return size >= 2 && data[0] == 0x1F && data[1] == 0x8B;
}

/// Decompress an RFC 1952 gzip stream via miniz raw inflate.
/// The trailer's ISIZE sizes the output buffer exactly (bounding a
/// decompression bomb via kMaxInflatedSize) and its CRC32 is verified,
/// so corrupt data fails cleanly instead of parsing as garbage.
/// Returns an empty vector on failure.
std::vector<uint8_t> gunzip(const uint8_t *data, size_t size) {
  if (size < 18 || data[2] != 8) // 8 = DEFLATE compression method
    return {};
  uint8_t flags = data[3];
  if (flags & 0xE0) // reserved FLG bits (RFC 1952 §2.3.1.2)
    return {};
  size_t pos = 10; // fixed header
  if (flags & 0x04) { // FEXTRA
    if (pos + 2 > size)
      return {};
    pos += 2 + (data[pos] | (data[pos + 1] << 8));
  }
  if (flags & 0x08) { // FNAME (NUL-terminated)
    while (pos < size && data[pos])
      ++pos;
    ++pos;
  }
  if (flags & 0x10) { // FCOMMENT (NUL-terminated)
    while (pos < size && data[pos])
      ++pos;
    ++pos;
  }
  if (flags & 0x02) // FHCRC
    pos += 2;
  if (pos + 8 >= size) // need deflate data plus the 8-byte trailer
    return {};

  uint32_t want_crc = rd32(data + size - 8);
  uint32_t want_size = rd32(data + size - 4);
  if (want_size > kMaxInflatedSize)
    return {};

  std::vector<uint8_t> out(want_size);
  size_t got = tinfl_decompress_mem_to_mem(out.data(), out.size(), data + pos,
                                           size - pos - 8, 0 /* raw deflate */);
  if (got != want_size) // includes TINFL_DECOMPRESS_MEM_TO_MEM_FAILED
    return {};
  if (static_cast<uint32_t>(
          mz_crc32(MZ_CRC32_INIT, out.data(), out.size())) != want_crc)
    return {};
  return out;
}

// ---- YM2612 shadow state ----

struct OpState {
  uint8_t dt = 0, ml = 0, tl = 0, ks = 0, ar = 0, am = 0, dr = 0, sr = 0,
          sl = 0, rr = 0, ssg = 0;
  bool operator==(const OpState &) const = default;
};

struct ChannelState {
  std::array<OpState, 4> op{};
  uint8_t algorithm = 0, feedback = 0, pms = 0, ams = 0;
  bool operator==(const ChannelState &) const = default;
};

/// A channel state captured at key-on, plus the global LFO register
/// ($22) at that moment.
struct Snapshot {
  ChannelState state;
  uint8_t lfo = 0; // bits 0-2 = frequency, bit 3 = enable
};

/// Exact-dedup key: the raw bytes of a ChannelState.  All members are
/// uint8_t, so the representation is dense and padding-free.
static_assert(std::is_trivially_copyable_v<ChannelState> &&
              sizeof(ChannelState) == 48);
std::string state_key(const ChannelState &s) {
  return {reinterpret_cast<const char *>(&s), sizeof(s)};
}

/// Bitmask of carrier (output) operator slots per algorithm, in
/// register-slot order (bit 0 = slot 0 = OP1, ..., bit 3 = slot 3 = OP4).
int carrier_mask(uint8_t algorithm) {
  static constexpr int masks[8] = {0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF};
  return masks[algorithm & 7];
}

/// True if the state can never produce sound: no operator has an
/// attack, or every operator is fully attenuated.
bool is_silent(const ChannelState &s) {
  bool no_attack = true, all_muted = true;
  for (const auto &o : s.op) {
    if (o.ar != 0)
      no_attack = false;
    if (o.tl < 0x7F)
      all_muted = false;
  }
  return no_attack || all_muted;
}

/// Instrument identity: everything except carrier TL (the driver's
/// volume control).  FMS/AMS, per-op AM-EN and LFO are also excluded —
/// they change with musical expression (vibrato/tremolo) and would
/// split one instrument into many near-duplicates.  Matches vgm2pre's
/// default (non-extended) comparison.
bool same_instrument(const ChannelState &a, const ChannelState &b) {
  if (a.algorithm != b.algorithm || a.feedback != b.feedback)
    return false;
  int carriers = carrier_mask(a.algorithm);
  for (int i = 0; i < 4; ++i) {
    const OpState &o1 = a.op[i];
    const OpState &o2 = b.op[i];
    if (!(carriers & (1 << i)) && o1.tl != o2.tl)
      return false;
    if (o1.ar != o2.ar || o1.dr != o2.dr || o1.sr != o2.sr ||
        o1.rr != o2.rr || o1.sl != o2.sl || o1.ml != o2.ml ||
        o1.dt != o2.dt || o1.ssg != o2.ssg || o1.ks != o2.ks)
      return false;
  }
  return true;
}

/// Total carrier output level; higher = louder.
int loudness(const ChannelState &s) {
  int carriers = carrier_mask(s.algorithm);
  int v = 0;
  for (int i = 0; i < 4; ++i)
    if (carriers & (1 << i))
      v += 127 - s.op[i].tl;
  return v;
}

class Ym2612Tracker {
public:
  void write(int port, uint8_t reg, uint8_t val) {
    if (port == 0) {
      switch (reg) {
      case 0x22: // LFO enable + frequency
        lfo_ = val & 0x0F;
        return;
      case 0x28: // key on/off
        key_on(val);
        return;
      case 0x2B: // DAC enable
        dac_on_ = (val & 0x80) != 0;
        return;
      default:
        break;
      }
    }
    if (reg < 0x30 || (reg & 3) == 3)
      return;
    ChannelState &c = ch_[(reg & 3) + port * 3];
    OpState &op = c.op[(reg >> 2) & 3];
    switch (reg & 0xF0) {
    case 0x30: // DT / MUL
      op.dt = (val >> 4) & 7;
      op.ml = val & 0x0F;
      break;
    case 0x40: // TL
      op.tl = val & 0x7F;
      break;
    case 0x50: // KS / AR
      op.ks = (val >> 6) & 3;
      op.ar = val & 0x1F;
      break;
    case 0x60: // AM / DR
      op.am = (val >> 7) & 1;
      op.dr = val & 0x1F;
      break;
    case 0x70: // SR
      op.sr = val & 0x1F;
      break;
    case 0x80: // SL / RR
      op.sl = val >> 4;
      op.rr = val & 0x0F;
      break;
    case 0x90: // SSG-EG
      op.ssg = val & 0x0F;
      break;
    case 0xB0:
      if (reg <= 0xB2) { // FB / ALG
        c.algorithm = val & 7;
        c.feedback = (val >> 3) & 7;
      } else if (reg <= 0xB6) { // pan / AMS / FMS
        // Pan (bits 6-7) is performance data; output normalizes L=R=on.
        c.pms = val & 7;
        c.ams = (val >> 4) & 3;
      }
      break;
    default: // $A0-$AF frequency — not part of the patch
      break;
    }
  }

  const std::vector<Snapshot> &snapshots() const { return collected_; }

private:
  void key_on(uint8_t val) {
    int c = val & 7; // 0-2 = ch1-3, 4-6 = ch4-6; 3 and 7 are invalid
    if (c == 3 || c == 7)
      return;
    int ch = (c > 3) ? c - 1 : c;
    if (ch == 5 && dac_on_)
      return;
    if ((val & 0xF0) == 0) // key-off only
      return;
    const ChannelState &s = ch_[ch];
    if (is_silent(s))
      return;
    auto [it, inserted] = seen_.try_emplace(state_key(s), collected_.size());
    if (inserted) {
      collected_.push_back({s, lfo_});
    } else if ((lfo_ & 0x08) && !(collected_[it->second].lfo & 0x08)) {
      // Same state re-keyed with the LFO now enabled (e.g. an init
      // key-on before the driver turns the LFO on): prefer the
      // LFO-carrying snapshot so the patch's FMS/AMS stay meaningful.
      collected_[it->second].lfo = lfo_;
    }
  }

  std::array<ChannelState, 6> ch_{};
  bool dac_on_ = false;
  uint8_t lfo_ = 0;
  std::vector<Snapshot> collected_;
  std::unordered_map<std::string, size_t> seen_; ///< state key → collected_ index
};

/// Group snapshots that are the same instrument at different volumes
/// and keep the loudest variant of each, in first-appearance order.
/// Groups whose every variant has all carriers muted are dropped.
/// same_instrument is an equivalence relation (algorithm/feedback and
/// all non-carrier-TL parameters are equal within a group), so any
/// member can stand in as the group's representative for matching.
std::vector<Snapshot> group_and_pick(const std::vector<Snapshot> &all) {
  struct Group {
    Snapshot best;
    int best_v;
  };
  std::vector<Group> groups;
  for (const auto &snap : all) {
    Group *g = nullptr;
    for (auto &existing : groups) {
      if (same_instrument(existing.best.state, snap.state)) {
        g = &existing;
        break;
      }
    }
    int v = loudness(snap.state);
    if (!g)
      groups.push_back({snap, v});
    else if (v > g->best_v) // strict: ties keep the earlier variant
      *g = {snap, v};
  }

  std::vector<Snapshot> picked;
  for (const auto &g : groups)
    if (g.best_v > 0)
      picked.push_back(g.best);
  return picked;
}

Patch to_patch(const Snapshot &s, std::string name) {
  Patch p;
  p.name = std::move(name);
  p.algorithm = s.state.algorithm;
  p.feedback = s.state.feedback;
  p.fms = s.state.pms;
  p.ams = s.state.ams;
  p.left = true; // pan normalized (see header)
  p.right = true;
  p.lfo_enable = (s.lfo & 0x08) != 0;
  p.lfo_frequency = s.lfo & 0x07;
  p.dac_enable = false;
  for (int i = 0; i < 4; ++i) {
    const OpState &o = s.state.op[i];
    auto &op = p.operators[i];
    op.ml = o.ml;
    op.dt = o.dt; // hardware encoding, kept as-is
    op.tl = o.tl;
    op.ks = o.ks;
    op.ar = o.ar;
    op.dr = o.dr;
    op.sr = o.sr;
    op.sl = o.sl;
    op.rr = o.rr;
    op.am = o.am != 0;
    op.ssg_enable = (o.ssg & 0x08) != 0;
    op.ssg = o.ssg & 0x07;
    op.enable = true;
  }
  return p;
}

// ---- VGM command stream ----

/// Operand byte count for commands other than the specially-handled
/// 0x52/0x53/0x66/0x67/0x68.  Returns -1 for undefined commands.
int operand_length(uint8_t cmd, uint32_t version) {
  if (cmd >= 0x30 && cmd <= 0x3F)
    return 1; // reserved, one operand
  if (cmd >= 0x40 && cmd <= 0x4E)
    return version >= 0x161 ? 2 : 1; // reserved; grew to 2 operands in 1.61
  if (cmd == 0x4F || cmd == 0x50)
    return 1; // Game Gear stereo / PSG
  if (cmd >= 0x51 && cmd <= 0x5F)
    return 2; // FM chip writes
  if (cmd == 0x61)
    return 2; // wait nn nn
  if (cmd == 0x62 || cmd == 0x63)
    return 0; // wait 735 / 882
  if (cmd == 0x64)
    return 3; // override wait length
  if (cmd == 0x68)
    return 11; // PCM RAM write: 0x66 cc oo oo oo dd dd dd ss ss ss
  if (cmd >= 0x70 && cmd <= 0x8F)
    return 0; // short waits / DAC write+wait
  switch (cmd) { // DAC stream control (VGM 1.60)
  case 0x90: return 4;
  case 0x91: return 4;
  case 0x92: return 5;
  case 0x93: return 10;
  case 0x94: return 1;
  case 0x95: return 4;
  default: break;
  }
  if (cmd >= 0xA0 && cmd <= 0xBF)
    return 2; // AY8910, second-chip FM writes, PWM, etc.
  if (cmd >= 0xC0 && cmd <= 0xDF)
    return 3;
  if (cmd >= 0xE0) // through 0xFF
    return 4;
  return -1; // 0x00-0x2F, 0x65, 0x69-0x6F, 0x96-0x9F: undefined
}

} // namespace

FormatInfo info() {
  return {Format::Vgm, "VGM/VGZ register log", "vgm", true, false, false,
          {"vgz"}};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  std::vector<uint8_t> decompressed;
  const uint8_t *d = data;
  size_t n = size;
  if (is_gzip(data, size)) {
    decompressed = gunzip(data, size);
    if (decompressed.empty())
      return Error{"Failed to decompress VGZ (gzip) data"};
    d = decompressed.data();
    n = decompressed.size();
  }

  if (n < 0x40 || std::memcmp(d, "Vgm ", 4) != 0)
    return Error{"Not a VGM file (missing 'Vgm ' magic)"};

  uint32_t version = rd32(d + 0x08);
  // The v1.50+ data offset is relative to its own field at 0x34.
  // Validate in 64 bits (a corrupt value must not wrap size_t on
  // 32-bit targets) and reject offsets landing inside the header.
  uint64_t data_start = 0x40;
  if (version >= 0x150) {
    uint32_t rel = rd32(d + 0x34);
    if (rel)
      data_start = 0x34 + static_cast<uint64_t>(rel);
  }
  if (data_start < 0x40 || data_start >= n)
    return Error{"VGM data offset out of bounds"};
  size_t pos = static_cast<size_t>(data_start);

  std::vector<std::string> warnings;
  Ym2612Tracker tracker;

  while (pos < n) {
    uint8_t cmd = d[pos++];

    if (cmd == 0x66) // end of sound data
      break;
    if (cmd == 0x67) { // data block: 0x66 tt ss ss ss ss <data>
      if (pos + 6 > n) {
        warnings.push_back("VGM data truncated mid-command");
        break;
      }
      // Bit 31 of the size is the second-chip flag (VGM 1.51+).
      uint64_t block_size = rd32(d + pos + 2) & 0x7FFFFFFF;
      if (pos + 6 + block_size > n) {
        warnings.push_back("VGM data block runs past end of file");
        break;
      }
      pos += 6 + static_cast<size_t>(block_size);
      continue;
    }

    int len = operand_length(cmd, version);
    if (len < 0) {
      char hex[3];
      std::snprintf(hex, sizeof(hex), "%02X", cmd);
      warnings.push_back(std::string("Unknown VGM command 0x") + hex +
                         "; stopping scan");
      break;
    }
    if (pos + static_cast<size_t>(len) > n) {
      warnings.push_back("VGM data truncated mid-command");
      break;
    }
    if (cmd == 0x52 || cmd == 0x53) // YM2612 port 0 / port 1 write
      tracker.write(cmd - 0x52, d[pos], d[pos + 1]);
    pos += static_cast<size_t>(len);
  }

  auto picked = group_and_pick(tracker.snapshots());

  std::string base = fallback_name.empty() ? "vgm" : fallback_name;
  std::vector<Patch> patches;
  patches.reserve(picked.size());
  for (size_t i = 0; i < picked.size(); ++i)
    patches.push_back(to_patch(picked[i], base + "_" + std::to_string(i)));

  if (patches.empty()) {
    if (tracker.snapshots().empty())
      warnings.push_back("No YM2612 instruments found (no FM key-on events)");
    else
      warnings.push_back("No audible YM2612 instruments found (every "
                         "captured state has all carrier operators muted)");
  }

  return ParseOk{std::move(patches), std::move(warnings)};
}

} // namespace ym2612_format::vgm
