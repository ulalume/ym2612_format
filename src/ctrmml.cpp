#include "ym2612_format/ctrmml.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ym2612_format::ctrmml {

FormatInfo info() { return {Format::Mml, "ctrmml (MML)", "mml", true, true, true}; }

namespace {

// Operator slot order: display → storage index
constexpr uint8_t op_slot_order[4] = {0, 2, 1, 3};

std::string trim(const std::string &s) {
  auto first = std::find_if_not(s.begin(), s.end(),
                                 [](unsigned char ch) { return std::isspace(ch); });
  auto last = std::find_if_not(s.rbegin(), s.rend(),
                                [](unsigned char ch) { return std::isspace(ch); })
                  .base();
  return (first >= last) ? std::string{} : std::string(first, last);
}

std::vector<int> parse_numbers(const std::string &text) {
  std::vector<int> result;
  const char *p = text.c_str();
  while (*p) {
    // Skip whitespace and commas
    while (*p && (*p == ' ' || *p == '\t' || *p == ','))
      ++p;
    if (!*p) break;
    char *end;
    long v = std::strtol(p, &end, 10);
    if (end == p) break; // not a number
    result.push_back(static_cast<int>(v));
    p = end;
  }
  return result;
}

uint8_t clamp_u8(int v, int lo, int hi) {
  return static_cast<uint8_t>(std::clamp(v, lo, hi));
}

bool starts_with_at(const std::string &line) {
  auto first = std::find_if_not(line.begin(), line.end(),
                                 [](unsigned char ch) { return std::isspace(ch); });
  return first != line.end() && *first == '@';
}

// ---- Pitch macro → @M node conversion ----
// Furnace pitch unit: 256 = 1 semitone (same scale as MDSDRV @M).

std::string format_semitones(double v) {
  if (v == 0.0)
    return "0";
  double r = std::round(v * 100.0) / 100.0;
  // Integer
  if (r == static_cast<int>(r))
    return std::to_string(static_cast<int>(r));
  // 1 decimal place if sufficient
  double r1 = std::round(r * 10.0) / 10.0;
  char buf[32];
  std::snprintf(buf, sizeof(buf), r1 == r ? "%.1f" : "%.2f", r);
  return buf;
}

/// Compress a pitch macro value sequence into ctrmml @M node notation.
/// Detects constant holds (value:ticks) and linear slopes (start>end:ticks).
std::string pitch_macro_to_mml_nodes(const Macro &m) {
  if (m.empty())
    return {};

  const auto &vals = m.values;
  unsigned speed = std::max<unsigned>(1, m.speed);

  std::string out;
  size_t i = 0;
  bool first = true;

  while (i < vals.size()) {
    // Insert loop marker before the loop index
    if (m.loop != 255 && i == static_cast<size_t>(m.loop)) {
      if (!first)
        out += ' ';
      out += '|';
      first = false;
    }

    if (!first)
      out += ' ';
    first = false;

    // Find longest linear run from i (consecutive constant delta).
    // Do not extend past the loop boundary so | lands correctly.
    size_t run_end = i + 1;
    if (i + 1 < vals.size()) {
      int32_t delta = vals[i + 1] - vals[i];
      size_t j = i + 2;
      while (j < vals.size() && vals[j] - vals[j - 1] == delta) {
        if (m.loop != 255 && j == static_cast<size_t>(m.loop))
          break;
        ++j;
      }
      run_end = j;
    }

    size_t run_len = run_end - i;
    double start_st = vals[i] / 256.0;
    unsigned ticks = static_cast<unsigned>(run_len) * speed;

    if (run_len >= 2) {
      double end_st = vals[run_end - 1] / 256.0;
      if (start_st == end_st) {
        // Constant hold
        out += format_semitones(start_st);
        out += ':';
        out += std::to_string(ticks);
      } else {
        // Linear slope
        out += format_semitones(start_st);
        out += '>';
        out += format_semitones(end_st);
        out += ':';
        out += std::to_string(ticks);
      }
      i = run_end;
    } else {
      // Single value
      out += format_semitones(start_st);
      if (speed > 1) {
        out += ':';
        out += std::to_string(speed);
      }
      ++i;
    }
  }

  return out;
}

// ---- Line splitting helper ----

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

// ---- Header parsing helper ----

struct HeaderParse {
  char at_sign;
  int instrument_number;
  std::string fm_token;
  std::vector<int> inline_numbers;
  bool valid;
};

HeaderParse parse_header(const std::string &s) {
  HeaderParse h{};
  h.valid = false;

  const char *p = s.c_str();
  // Skip whitespace
  while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
  if (*p != '@') return h;
  h.at_sign = *p++;

  // Parse instrument number
  while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
  char *end;
  long num = std::strtol(p, &end, 10);
  if (end == p) return h;
  h.instrument_number = static_cast<int>(num);
  p = end;

  // Parse fm token
  while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
  while (*p && !std::isspace(static_cast<unsigned char>(*p))) {
    h.fm_token += *p++;
  }

  // Parse remaining inline numbers
  while (*p) {
    while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    if (!*p) break;
    long v = std::strtol(p, &end, 10);
    if (end == p) break;
    h.inline_numbers.push_back(static_cast<int>(v));
    p = end;
  }

  h.valid = true;
  return h;
}

} // namespace

ParseResult parse(const uint8_t *data, size_t size, const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  std::string text(reinterpret_cast<const char *>(data), size);
  auto lines = split_lines(text);

  std::vector<Patch> patches;
  std::vector<std::string> warnings;

  for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
    std::string trimmed = trim(lines[line_index]);
    if (trimmed.empty() || trimmed.front() != '@')
      continue;

    // Extract comment as potential name
    std::string comment;
    auto comment_pos = lines[line_index].find(';');
    if (comment_pos != std::string::npos)
      comment = trim(lines[line_index].substr(comment_pos + 1));

    std::string before_comment =
        (comment_pos == std::string::npos)
            ? lines[line_index]
            : lines[line_index].substr(0, comment_pos);

    auto header = parse_header(before_comment);
    if (!header.valid || header.at_sign != '@')
      continue;

    std::string fm_lower = header.fm_token;
    std::transform(fm_lower.begin(), fm_lower.end(), fm_lower.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    if (fm_lower != "fm")
      continue;

    int algorithm = -1, feedback_val = -1;
    if (!header.inline_numbers.empty()) {
      algorithm = header.inline_numbers[0];
      if (header.inline_numbers.size() > 1)
        feedback_val = header.inline_numbers[1];
    }

    // Read operator rows
    std::vector<std::array<int, 10>> operator_rows;
    size_t consumed_index = line_index;

    for (size_t next = line_index + 1; next < lines.size(); ++next) {
      std::string next_trimmed = trim(lines[next]);

      if (next_trimmed.empty()) {
        consumed_index = next;
        continue;
      }
      if (starts_with_at(next_trimmed)) {
        consumed_index = next - 1;
        break;
      }

      auto nc_pos = next_trimmed.find(';');
      std::string data_part =
          nc_pos == std::string::npos
              ? next_trimmed
              : trim(next_trimmed.substr(0, nc_pos));

      if (data_part.empty()) {
        consumed_index = next;
        continue;
      }

      auto numbers = parse_numbers(data_part);
      if (numbers.empty()) {
        consumed_index = next;
        continue;
      }

      if (algorithm < 0 && numbers.size() >= 2) {
        algorithm = numbers[0];
        feedback_val = numbers[1];
        consumed_index = next;
        continue;
      }

      if (numbers.size() >= 10) {
        std::array<int, 10> row{};
        for (size_t idx = 0; idx < 10; ++idx)
          row[idx] = numbers[idx];
        operator_rows.push_back(row);
        consumed_index = next;
        if (operator_rows.size() == 4) {
          // Skip trailing blank/comment lines
          ++next;
          while (next < lines.size()) {
            std::string peek = trim(lines[next]);
            if (peek.empty() || peek.front() == ';') {
              consumed_index = next;
              ++next;
              continue;
            }
            break;
          }
          break;
        }
        continue;
      }

      consumed_index = next;
    }

    line_index = consumed_index;

    if (algorithm < 0) algorithm = 0;
    if (feedback_val < 0) feedback_val = 0;

    if (operator_rows.size() != 4) {
      warnings.push_back("Instrument @" + std::to_string(operator_rows.size()) +
                          " does not contain 4 operator rows, skipping");
      continue;
    }

    Patch patch;
    patch.left = true;
    patch.right = true;
    patch.algorithm = clamp_u8(algorithm, 0, 7);
    patch.feedback = clamp_u8(feedback_val, 0, 7);

    for (size_t op_idx = 0; op_idx < 4; ++op_idx) {
      const auto &row = operator_rows[op_idx];
      auto &op = patch.operators[op_slot_order[op_idx]];

      op.ar = clamp_u8(row[0], 0, 31);
      op.dr = clamp_u8(row[1], 0, 31);
      op.sr = clamp_u8(row[2], 0, 31);
      op.rr = clamp_u8(row[3], 0, 15);
      op.sl = clamp_u8(row[4], 0, 15);
      op.tl = clamp_u8(row[5], 0, 127);
      op.ks = clamp_u8(row[6], 0, 3);
      op.ml = clamp_u8(row[7], 0, 15);
      op.dt = clamp_u8(row[8], 0, 7);

      int ssg_value = row[9];
      op.am = ssg_value >= 100;
      if (op.am) ssg_value -= 100;
      op.ssg_enable = (ssg_value & 0b1000) != 0;
      op.ssg = ssg_value & 0b0111;
    }

    std::string inst_name = comment;
    if (inst_name.empty()) {
      inst_name = fallback_name.empty()
                      ? ("instrument_" + std::to_string(header.instrument_number))
                      : (fallback_name + "_" +
                         std::to_string(header.instrument_number));
    }
    patch.name = inst_name;
    patches.push_back(std::move(patch));
  }

  if (patches.empty())
    return Error{"No MML instrument definitions found"};

  return ParseOk{std::move(patches), std::move(warnings)};
}

SerializeTextResult serialize_text(const Patch &patch) {
  std::string out;
  out.reserve(512);
  char buf[128];

  std::string inst_name = patch.name.empty() ? "Instrument" : patch.name;

  out += "@1 fm ; ";
  out += inst_name;
  out += '\n';
  out += "; ALG  FB\n";
  std::snprintf(buf, sizeof(buf), "   %2d   %d\n",
                static_cast<int>(patch.algorithm),
                static_cast<int>(patch.feedback));
  out += buf;
  out += ";  AR  DR  SR  RR  SL  TL  KS  ML  DT SSG\n";

  const std::array<const char *, 4> op_labels = {"OP1", "OP2", "OP3", "OP4"};

  for (size_t op_idx = 0; op_idx < 4; ++op_idx) {
    const auto &op = patch.operators[op_slot_order[op_idx]];

    int ssg_value = op.ssg & 0x07;
    if (op.ssg_enable) ssg_value += 8;
    if (op.am) ssg_value += 100;

    std::snprintf(buf, sizeof(buf),
                  "   %2d %3d %3d %3d %3d %3d %3d %3d %3d %3d ; %s\n",
                  static_cast<int>(op.ar), static_cast<int>(op.dr),
                  static_cast<int>(op.sr), static_cast<int>(op.rr),
                  static_cast<int>(op.sl), static_cast<int>(op.tl),
                  static_cast<int>(op.ks), static_cast<int>(op.ml),
                  static_cast<int>(op.dt), ssg_value,
                  op_labels[op_idx]);
    out += buf;
  }

  // LFO info as comment
  if (patch.ams != 0 || patch.fms != 0) {
    std::snprintf(buf, sizeof(buf), "; 'lforate %d' 'lfo %d %d' ; LFO\n",
                  patch.lfo_enable ? static_cast<int>(patch.lfo_frequency) + 1 : 0,
                  static_cast<int>(patch.ams),
                  static_cast<int>(patch.fms));
    out += buf;
  }

  // Panning as comment
  if (!patch.left || !patch.right) {
    auto p = patch.right + (patch.left << 1);
    std::snprintf(buf, sizeof(buf), "; p%d ; Panning\n", p);
    out += buf;
  }

  // Pitch macro as @M comment
  if (!patch.macros.pitch.empty()) {
    out += "; @M1 ";
    out += pitch_macro_to_mml_nodes(patch.macros.pitch);
    out += " ; pitch\n";
  }

  return out;
}

SerializeResult serialize(const Patch &patch) {
  auto result = serialize_text(patch);
  if (!is_ok(result))
    return get_error(result);
  const auto &text = get_ok(result);
  return std::vector<uint8_t>(text.begin(), text.end());
}

} // namespace ym2612_format::ctrmml
