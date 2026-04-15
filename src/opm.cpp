#include "ym2612_format/opm.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ym2612_format::opm {

FormatInfo info() {
  return {Format::Opm, "VOPM/MiOPMdrv", "opm", true, false, true};
}

namespace {

// OPM operator label → YM2612 slot index (Furnace convention).
//   M1 → 0 (modulator 1)
//   C1 → 2 (carrier 1 / modulator 3)
//   M2 → 1 (modulator 2)
//   C2 → 3 (carrier 2)
int op_label_to_slot(const std::string &label) {
  if (label == "M1") return 0;
  if (label == "M2") return 1;
  if (label == "C1") return 2;
  if (label == "C2") return 3;
  return -1;
}

std::string trim(const std::string &s) {
  auto first = std::find_if_not(s.begin(), s.end(),
                                [](unsigned char ch) { return std::isspace(ch); });
  auto last = std::find_if_not(s.rbegin(), s.rend(),
                               [](unsigned char ch) { return std::isspace(ch); })
                  .base();
  return (first >= last) ? std::string{} : std::string(first, last);
}

/// Strip `//` line comments anywhere in the line.  OPM files frequently
/// contain inline comments after the data.
std::string strip_line_comment(const std::string &s) {
  auto pos = s.find("//");
  if (pos == std::string::npos)
    return s;
  return s.substr(0, pos);
}

std::vector<int> parse_numbers(const std::string &text) {
  std::vector<int> result;
  const char *p = text.c_str();
  while (*p) {
    while (*p && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',' ||
                  *p == ':'))
      ++p;
    if (!*p) break;
    char *end;
    long v = std::strtol(p, &end, 10);
    if (end == p) { ++p; continue; }
    result.push_back(static_cast<int>(v));
    p = end;
  }
  return result;
}

uint8_t clamp_u8(int v, int lo, int hi) {
  return static_cast<uint8_t>(std::clamp(v, lo, hi));
}

std::vector<std::string> split_lines(const std::string &text) {
  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < text.size()) {
    auto nl = text.find('\n', pos);
    std::string line;
    if (nl == std::string::npos) {
      line = text.substr(pos);
      pos = text.size();
    } else {
      line = text.substr(pos, nl - pos);
      pos = nl + 1;
    }
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(std::move(line));
  }
  return lines;
}

/// Split a line at its first ':' to yield (tag, body).
/// Returns false if no ':' is present.
bool split_tag(const std::string &line, std::string &tag, std::string &body) {
  auto colon = line.find(':');
  if (colon == std::string::npos)
    return false;
  tag = trim(line.substr(0, colon));
  body = line.substr(colon + 1);
  return true;
}

std::string uppercase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return s;
}

struct Builder {
  Patch patch;
  bool started = false;
  int instrument_number = 0;
  int ops_seen = 0;

  // Deferred OPM CH parameters (applied at flush, gated by LFO AMD/PMD).
  bool ch_seen = false;
  int ch_ams = 0;
  int ch_pms = 0;

  // Deferred OPM LFO parameters.
  bool lfo_seen = false;
  int lfo_lfrq = 0;
  int lfo_amd = 0;
  int lfo_pmd = 0;
  int lfo_wf = 0;
  int lfo_nfrq = 0;
};

// Map OPM LFRQ (0-255) to OPN2 lfo_frequency (0-7).  This is a best-effort
// proportional mapping: the two chips use completely different LFO clocks
// (OPM spans ~0.01-200Hz non-linearly via an 8-bit register; OPN2 has a
// fixed 8-step table at ~4-72Hz).  No mapping preserves absolute Hz
// accurately, so we keep a trivial linear scale which at least preserves
// the relative "slow vs fast" intent of the original patch.  Users who
// need precise LFO speed should tune lfo_frequency manually after import.
uint8_t map_lfrq_to_opn2(int lfrq) {
  int idx = lfrq >> 5; // 256 → 8 bins
  return static_cast<uint8_t>(std::clamp(idx, 0, 7));
}

} // namespace

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  std::string text(reinterpret_cast<const char *>(data), size);

  // Cheap sniff — require the MiOPMdrv signature or at least an "@:" header
  // followed by typical OPM tags.  This keeps format auto-detection from
  // accidentally eating unrelated text files.
  {
    std::string head = text.substr(0, std::min<size_t>(text.size(), 4096));
    bool looks_like_opm =
        head.find("MiOPMdrv") != std::string::npos ||
        (head.find("@:") != std::string::npos &&
         (head.find("\nM1:") != std::string::npos ||
          head.find("\nC1:") != std::string::npos));
    if (!looks_like_opm)
      return Error{"Not an OPM/MiOPMdrv file"};
  }

  auto lines = split_lines(text);
  std::vector<Patch> patches;
  std::vector<std::string> warnings;

  Builder cur;
  bool warned_lfo = false;
  bool warned_dt2 = false;
  bool warned_ne = false;
  bool warned_slot = false;

  auto flush = [&]() {
    if (!cur.started)
      return;
    if (cur.ops_seen < 4) {
      warnings.push_back("Instrument '" + cur.patch.name +
                         "' has only " + std::to_string(cur.ops_seen) +
                         " operator lines; remaining slots left at defaults");
    }

    // Resolve OPM LFO + CH interaction.  In OPM, the per-channel AMS/PMS
    // only produce audible modulation when the global AMD/PMD depth is
    // also non-zero.  OPN2 has no AMD/PMD, so blindly copying AMS/PMS
    // would introduce modulation that wasn't present in the source
    // patch.  Gate on AMD/PMD explicitly.
    if (cur.ch_seen) {
      bool amd_active = cur.lfo_seen && cur.lfo_amd > 0;
      bool pmd_active = cur.lfo_seen && cur.lfo_pmd > 0;
      cur.patch.ams = amd_active ? clamp_u8(cur.ch_ams, 0, 3) : 0;
      cur.patch.fms = pmd_active ? clamp_u8(cur.ch_pms, 0, 7) : 0;
    }

    // Enable OPN2 LFO if the OPM patch has any LFO intent (non-zero LFRQ
    // or non-zero depth).  lfo_frequency is derived from LFRQ with a
    // coarse approximation — see map_lfrq_to_opn2.
    bool has_lfo_intent = cur.lfo_seen && (cur.lfo_lfrq > 0 ||
                                           cur.lfo_amd > 0 ||
                                           cur.lfo_pmd > 0);
    if (has_lfo_intent) {
      cur.patch.lfo_enable = true;
      cur.patch.lfo_frequency = map_lfrq_to_opn2(cur.lfo_lfrq);
    }

    patches.push_back(std::move(cur.patch));
    cur = Builder{};
  };

  for (const auto &raw : lines) {
    std::string line = trim(strip_line_comment(raw));
    if (line.empty())
      continue;

    std::string tag, body;
    if (!split_tag(line, tag, body))
      continue;

    std::string utag = uppercase(tag);

    if (utag == "@") {
      // New instrument header: "@:N Name"
      flush();
      cur.started = true;
      cur.patch = Patch{};
      cur.patch.left = true;
      cur.patch.right = true;

      auto body_trim = trim(body);
      // Body is "<num> <name...>"
      size_t sp = 0;
      while (sp < body_trim.size() &&
             !std::isspace(static_cast<unsigned char>(body_trim[sp])))
        ++sp;
      std::string num_part = body_trim.substr(0, sp);
      std::string name_part =
          sp < body_trim.size() ? trim(body_trim.substr(sp)) : std::string{};

      if (!num_part.empty()) {
        char *end;
        long n = std::strtol(num_part.c_str(), &end, 10);
        if (end != num_part.c_str())
          cur.instrument_number = static_cast<int>(n);
      }
      if (!name_part.empty()) {
        cur.patch.name = name_part;
      } else if (!fallback_name.empty()) {
        cur.patch.name =
            fallback_name + "_" + std::to_string(cur.instrument_number);
      } else {
        cur.patch.name = "instrument_" + std::to_string(cur.instrument_number);
      }
      continue;
    }

    if (!cur.started)
      continue;

    if (utag == "LFO") {
      // LFRQ AMD PMD WF NFRQ — stash for deferred resolution at flush.
      auto nums = parse_numbers(body);
      if (nums.size() > 0) cur.lfo_lfrq = nums[0];
      if (nums.size() > 1) cur.lfo_amd  = nums[1];
      if (nums.size() > 2) cur.lfo_pmd  = nums[2];
      if (nums.size() > 3) cur.lfo_wf   = nums[3];
      if (nums.size() > 4) cur.lfo_nfrq = nums[4];
      cur.lfo_seen = true;

      // Emit a single, self-describing warning about the lossy mapping.
      // LFRQ maps approximately; AMD/PMD/WF/NFRQ have no OPN2 equivalent.
      bool any_nonzero = cur.lfo_lfrq || cur.lfo_amd || cur.lfo_pmd ||
                         cur.lfo_wf || cur.lfo_nfrq;
      if (any_nonzero && !warned_lfo) {
        warnings.push_back(
            "LFO LFRQ approximated to OPN2 lfo_frequency (OPM 0-255 \xE2\x86\x92 "
            "OPN2 0-7; absolute rates differ); AMD/PMD/WF/NFRQ have no "
            "OPN2 equivalent and were discarded");
        warned_lfo = true;
      }
      continue;
    }

    if (utag == "CH") {
      // PAN FL CON AMS PMS SLOT NE
      auto nums = parse_numbers(body);
      if (nums.size() < 5)
        continue;
      int pan = nums[0];
      int fl  = nums[1];
      int con = nums[2];
      int ams = nums[3];
      int pms = nums[4];
      int slot = nums.size() > 5 ? nums[5] : 120;
      int ne  = nums.size() > 6 ? nums[6] : 0;

      // PAN encoding on YM2151: bit7=L, bit6=R.  If the value doesn't
      // enable either channel we still default to stereo on YM2612,
      // which is the most useful behaviour for best-effort conversion.
      bool l = (pan & 0x80) != 0;
      bool r = (pan & 0x40) != 0;
      if (!l && !r) { l = true; r = true; }
      cur.patch.left = l;
      cur.patch.right = r;

      cur.patch.feedback = clamp_u8(fl, 0, 7);
      cur.patch.algorithm = clamp_u8(con, 0, 7);

      // AMS / PMS are applied at flush() once AMD/PMD are known, because
      // OPM's per-channel sensitivities only have audible effect when the
      // global LFO depth is non-zero.  Stash for later.
      cur.ch_ams = ams;
      cur.ch_pms = pms;
      cur.ch_seen = true;

      if (slot != 120 && slot != 0 && !warned_slot) {
        warnings.push_back(
            "SLOT mask is OPM-only; all four YM2612 operators will be keyed");
        warned_slot = true;
      }
      if (ne != 0 && !warned_ne) {
        warnings.push_back(
            "NE (noise enable) is OPM-only and was discarded");
        warned_ne = true;
      }
      continue;
    }

    int slot = op_label_to_slot(utag);
    if (slot < 0)
      continue;

    // AR D1R D2R RR D1L TL KS MUL DT1 DT2 AMS-EN
    auto nums = parse_numbers(body);
    if (nums.size() < 9)
      continue;

    auto &op = cur.patch.operators[slot];
    op.ar = clamp_u8(nums[0], 0, 31);
    op.dr = clamp_u8(nums[1], 0, 31);
    op.sr = clamp_u8(nums[2], 0, 31);
    op.rr = clamp_u8(nums[3], 0, 15);
    op.sl = clamp_u8(nums[4], 0, 15);
    op.tl = clamp_u8(nums[5], 0, 127);
    op.ks = clamp_u8(nums[6], 0, 3);
    op.ml = clamp_u8(nums[7], 0, 15);
    // OPM DT1 hardware encoding matches YM2612 exactly (0-7, signed in
    // the same magnitude/sign split), so we store it verbatim.
    op.dt = clamp_u8(nums[8], 0, 7);

    int dt2 = nums.size() > 9 ? nums[9] : 0;
    int amsen = nums.size() > 10 ? nums[10] : 0;

    if (dt2 != 0 && !warned_dt2) {
      warnings.push_back(
          "DT2 is OPM-only and was discarded (YM2612 has no DT2)");
      warned_dt2 = true;
    }

    op.am = amsen != 0;
    op.enable = true;
    ++cur.ops_seen;
  }

  flush();

  if (patches.empty())
    return Error{"No OPM instrument definitions found"};

  return ParseOk{std::move(patches), std::move(warnings)};
}

} // namespace ym2612_format::opm
