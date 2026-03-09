#include "ym2612_format/rym2612.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace ym2612_format::rym2612 {

FormatInfo info() { return {Format::Rym2612, "RYM2612 Preset", "rym2612", true, false, false}; }

namespace {

uint8_t convert_detune(int rym_dt) {
  switch (rym_dt) {
  case -3: return 7;
  case -2: return 6;
  case -1: return 5;
  case 0:  return 4;
  case 1:  return 1;
  case 2:  return 2;
  case 3:  return 3;
  default: return 4;
  }
}

uint8_t convert_multiple(int rym_mul) {
  float normalized = std::round(static_cast<float>(rym_mul) / 1000.0f);
  return static_cast<uint8_t>(std::clamp(normalized, 0.0f, 15.0f));
}

void convert_ssgeg(int rym_ssgeg, bool &ssg_enable, uint8_t &ssg_type) {
  if (rym_ssgeg >= 1 && rym_ssgeg <= 8) {
    ssg_enable = true;
    ssg_type = static_cast<uint8_t>(rym_ssgeg - 1);
  } else {
    ssg_enable = false;
    ssg_type = 0;
  }
}

std::string extract_xml_value(const std::string &xml, const std::string &tag) {
  std::string pattern = "<PARAM id=\"" + tag + "\" value=\"";
  size_t start = xml.find(pattern);
  if (start == std::string::npos) return "";
  start += pattern.length();
  size_t end = xml.find("\"", start);
  if (end == std::string::npos) return "";
  return xml.substr(start, end - start);
}

std::string extract_attribute(const std::string &xml,
                              const std::string &attr) {
  std::string pattern = attr + "=\"";
  size_t start = xml.find(pattern);
  if (start == std::string::npos) return "";
  start += pattern.length();
  size_t end = xml.find("\"", start);
  if (end == std::string::npos) return "";
  return xml.substr(start, end - start);
}

template <typename T>
T safe_convert(const std::string &str, T default_value = T{}) {
  try {
    if constexpr (std::is_same_v<T, int>)
      return std::stoi(str);
    else if constexpr (std::is_same_v<T, float>)
      return std::stof(str);
    else if constexpr (std::is_same_v<T, uint8_t>)
      return static_cast<uint8_t>(std::clamp(std::stoi(str), 0, 255));
  } catch (...) {
    return default_value;
  }
  return default_value;
}

// Operator slot order for RYM2612 (same as megatoy convention).
// Maps display operator index (1-4) to storage index.
constexpr uint8_t op_slot_order[4] = {0, 2, 1, 3};

} // namespace

ParseResult parse(const uint8_t *data, size_t size, const std::string &name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  std::string xml(reinterpret_cast<const char *>(data), size);

  try {
    Patch patch;

    std::string patch_name = extract_attribute(xml, "patchName");
    patch.name = patch_name.empty() ? name : patch_name;

    // Global
    patch.dac_enable = false;
    patch.lfo_enable =
        safe_convert<int>(extract_xml_value(xml, "LFO_Enable")) != 0;
    patch.lfo_frequency =
        safe_convert<uint8_t>(extract_xml_value(xml, "LFO_Speed"), 0);

    // Channel
    patch.left = true;
    patch.right = true;
    patch.ams = safe_convert<uint8_t>(extract_xml_value(xml, "AMS"), 0);
    patch.fms = safe_convert<uint8_t>(extract_xml_value(xml, "FMS"), 0);

    // Instrument
    patch.algorithm =
        safe_convert<uint8_t>(extract_xml_value(xml, "Algorithm"), 1) - 1;
    patch.feedback =
        safe_convert<uint8_t>(extract_xml_value(xml, "Feedback"), 0);

    for (int i = 1; i <= 4; ++i) {
      auto &op = patch.operators[op_slot_order[i - 1]];
      auto p = [&](const char *suffix) {
        return extract_xml_value(xml, "OP" + std::to_string(i) + suffix);
      };

      op.ar = safe_convert<uint8_t>(p("AR"), 0);
      op.dr = safe_convert<uint8_t>(p("D1R"), 0);
      op.sr = safe_convert<uint8_t>(p("D2R"), 0);
      op.rr = safe_convert<uint8_t>(p("RR"), 0);
      op.sl = 15 - safe_convert<uint8_t>(p("D2L"), 0);

      // Total level with velocity adjustment
      float base_tl = safe_convert<float>(p("TL"), 0);
      float velocity = safe_convert<float>(p("Vel"), 0);
      int final_tl = static_cast<int>(std::round(base_tl + velocity));
      op.tl = static_cast<uint8_t>(127 - std::clamp(final_tl, 0, 127));

      op.ks = safe_convert<uint8_t>(p("RS"), 0);
      op.ml = convert_multiple(safe_convert<int>(p("MUL"), 0));
      op.dt = convert_detune(safe_convert<int>(p("DT"), 0));

      int raw_ssgeg = safe_convert<int>(p("SSGEG"), 0);
      convert_ssgeg(raw_ssgeg, op.ssg_enable, op.ssg);

      op.am = safe_convert<int>(p("AM")) != 0;
    }

    return ParseOk{{patch}, {}};
  } catch (const std::exception &e) {
    return Error{std::string("RYM2612 parse error: ") + e.what()};
  }
}

} // namespace ym2612_format::rym2612
