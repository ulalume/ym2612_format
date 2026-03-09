#include "ym2612_format/dmp.hpp"

#include "ym2612_format/detune.hpp"
#include <algorithm>
#include <vector>

namespace ym2612_format::dmp {

FormatInfo info() {
  return {Format::Dmp, "DefleMask Preset", "dmp", true, true, false};
}

namespace {

uint8_t safe_read(const uint8_t *data, size_t size, size_t index) {
  return index < size ? data[index] : 0;
}

} // namespace

ParseResult parse(const uint8_t *data, size_t size, const std::string &name) {
  if (!data || size == 0) {
    return Error{"Empty data"};
  }

  std::vector<uint8_t> bytes(data, data + size);
  std::vector<std::string> warnings;

  constexpr size_t header_size = 7;
  constexpr size_t operator_bytes = 11;
  constexpr size_t operator_count = 4;
  constexpr size_t expected_size = header_size + operator_count * operator_bytes;

  // Heuristic repair for files missing version/system header bytes
  if (bytes.size() == expected_size - 2 && bytes.size() >= 3 && bytes[0] == 0 &&
      bytes[1] == 0 && bytes[2] == 0) {
    std::vector<uint8_t> repaired;
    repaired.reserve(expected_size);
    repaired.push_back(0x0B); // version
    repaired.push_back(0x02); // system: Genesis
    repaired.insert(repaired.end(), bytes.begin(), bytes.end());
    if (repaired.size() < expected_size) {
      repaired.resize(expected_size, 0);
    }
    if (repaired.size() >= 3 && repaired[2] == 0) {
      repaired[2] = 0x01; // FM instrument mode
    }
    warnings.push_back("Heuristically repaired missing DMP header bytes");
    bytes.swap(repaired);
  }

  if (bytes.size() < 7) {
    return Error{"DMP data too small to contain a header"};
  }

  struct HeaderLayout {
    size_t instrument_mode_idx;
    size_t system_idx;
    size_t fms_idx;
    size_t feedback_idx;
    size_t algorithm_idx;
    size_t ams_idx;
  };

  constexpr HeaderLayout modern_layout{2, 1, 3, 4, 5, 6};
  constexpr HeaderLayout legacy_v9_layout{1, 2, 3, 4, 5, 6};

  uint8_t file_version = bytes[0];
  const HeaderLayout *layout = &modern_layout;
  if (file_version == 0x09) {
    layout = &legacy_v9_layout;
  } else if (file_version != 0x0B) {
    warnings.push_back("Unrecognized DMP version " +
                        std::to_string(file_version) +
                        ", attempting best-effort parse");
  }

  uint8_t instrument_mode = safe_read(bytes.data(), bytes.size(),
                                       layout->instrument_mode_idx);
  uint8_t system =
      safe_read(bytes.data(), bytes.size(), layout->system_idx);
  uint8_t lfo_fms =
      safe_read(bytes.data(), bytes.size(), layout->fms_idx);
  uint8_t feedback_val =
      safe_read(bytes.data(), bytes.size(), layout->feedback_idx);
  uint8_t algorithm_val =
      safe_read(bytes.data(), bytes.size(), layout->algorithm_idx);
  uint8_t lfo_ams =
      safe_read(bytes.data(), bytes.size(), layout->ams_idx);

  if (bytes.size() < expected_size) {
    warnings.push_back("DMP data shorter than expected (" +
                        std::to_string(bytes.size()) + " < " +
                        std::to_string(expected_size) +
                        "), padding with zeros");
  }

  if (!(system == 0x02 ||
        (file_version <= 0x09 && (system == 0x00 || system == 0x01)))) {
    warnings.push_back("Unsupported DMP system code: " +
                        std::to_string(system));
  }

  if (instrument_mode != 1) {
    warnings.push_back("Instrument mode is not FM (" +
                        std::to_string(instrument_mode) +
                        "), parsing as FM anyway");
  }

  Patch patch;
  patch.name = name;
  patch.dac_enable = false;
  patch.lfo_enable = false;
  patch.lfo_frequency = 0;
  patch.left = true;
  patch.right = true;
  patch.ams = lfo_ams & 0x03;
  patch.fms = lfo_fms & 0x07;
  patch.algorithm = algorithm_val & 0x07;
  patch.feedback = feedback_val & 0x07;

  for (int op = 0; op < 4; ++op) {
    auto &o = patch.operators[op];
    const size_t base = header_size + op * operator_bytes;

    uint8_t mult = safe_read(bytes.data(), bytes.size(), base + 0);
    uint8_t tl = safe_read(bytes.data(), bytes.size(), base + 1);
    uint8_t ar = safe_read(bytes.data(), bytes.size(), base + 2);
    uint8_t dr = safe_read(bytes.data(), bytes.size(), base + 3);
    uint8_t sl = safe_read(bytes.data(), bytes.size(), base + 4);
    uint8_t rr = safe_read(bytes.data(), bytes.size(), base + 5);
    uint8_t am_val = safe_read(bytes.data(), bytes.size(), base + 6);
    uint8_t rs = safe_read(bytes.data(), bytes.size(), base + 7);
    uint8_t dt = safe_read(bytes.data(), bytes.size(), base + 8);
    uint8_t d2r = safe_read(bytes.data(), bytes.size(), base + 9);
    uint8_t ssgeg = safe_read(bytes.data(), bytes.size(), base + 10);

    o.ml = mult & 0x0F;
    o.tl = tl & 0x7F;
    o.ar = ar & 0x1F;
    o.dr = dr & 0x1F;
    o.sl = sl & 0x0F;
    o.rr = rr & 0x0F;
    o.am = (am_val != 0);
    o.ks = rs & 0x03;
    o.dt = detune_from_linear(dt);
    o.sr = d2r & 0x1F;
    o.ssg_enable = (ssgeg & 0x08) != 0;
    o.ssg = ssgeg & 0x07;
  }

  return ParseOk{{patch}, std::move(warnings)};
}

SerializeResult serialize(const Patch &patch) {
  std::vector<uint8_t> data;
  data.reserve(7 + 4 * 11);

  data.push_back(0x0B); // version
  data.push_back(0x02); // system: Genesis
  data.push_back(0x01); // instrument mode FM
  data.push_back(patch.fms & 0x07);
  data.push_back(patch.feedback & 0x07);
  data.push_back(patch.algorithm & 0x07);
  data.push_back(patch.ams & 0x03);

  for (int i = 0; i < 4; ++i) {
    const auto &op = patch.operators[i];
    data.push_back(op.ml & 0x0F);
    data.push_back(std::min<uint8_t>(op.tl, 127));
    data.push_back(std::min<uint8_t>(op.ar, 31));
    data.push_back(std::min<uint8_t>(op.dr, 31));
    data.push_back(std::min<uint8_t>(op.sl, 15));
    data.push_back(std::min<uint8_t>(op.rr, 15));
    data.push_back(op.am ? 1 : 0);
    data.push_back(std::min<uint8_t>(op.ks, 3));
    data.push_back(detune_to_linear(op.dt & 0x07));
    data.push_back(std::min<uint8_t>(op.sr, 31));
    uint8_t ssg_byte =
        (op.ssg_enable ? 0x08 : 0x00) | (op.ssg & 0x07);
    data.push_back(ssg_byte);
  }

  return data;
}

} // namespace ym2612_format::dmp
