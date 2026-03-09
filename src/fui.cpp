#include "ym2612_format/fui.hpp"

#include "ym2612_format/detune.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ym2612_format::fui {

FormatInfo info() { return {Format::Fui, "Furnace Instrument", "fui", true, true, false}; }

namespace {

constexpr std::array<char, 4> kFinsMagic{'F', 'I', 'N', 'S'};
constexpr std::array<char, 16> kOldMagic{'-', 'F', 'u', 'r', 'n', 'a',
                                          'c', 'e', ' ', 'i', 'n', 's',
                                          't', 'r', '.', '-'};

template <typename T>
T read_le(const uint8_t *buf, size_t size, size_t offset) {
  if (offset + sizeof(T) > size)
    return T{};
  T value{};
  for (size_t i = 0; i < sizeof(T); ++i)
    value |= static_cast<T>(buf[offset + i]) << (8 * i);
  return value;
}

bool parse_fm_feature(const uint8_t *data, size_t size, Patch &result) {
  if (size < 4)
    return false;

  const uint8_t flags = data[0];
  const uint8_t op_count =
      std::min<uint8_t>((flags & 0x0F) ? (flags & 0x0F) : 4, 4);

  result.algorithm = (data[1] >> 4) & 0x07;
  result.feedback = data[1] & 0x07;
  result.fms = data[2] & 0x07;
  result.ams = (data[2] >> 3) & 0x03;

  size_t offset = 4;
  constexpr size_t stride = 8;

  for (uint8_t i = 0; i < op_count; ++i, offset += stride) {
    if (offset + stride > size)
      return false;

    auto &op = result.operators[i];
    const uint8_t reg30 = data[offset + 0];
    const uint8_t reg40 = data[offset + 1];
    const uint8_t reg50 = data[offset + 2];
    const uint8_t reg60 = data[offset + 3];
    const uint8_t reg70 = data[offset + 4];
    const uint8_t reg80 = data[offset + 5];
    const uint8_t reg90 = data[offset + 6];

    op.dt = detune_from_linear(reg30 >> 4);
    op.ml = reg30 & 0x0F;
    op.tl = reg40 & 0x7F;
    op.ar = reg50 & 0x1F;
    op.dr = reg60 & 0x1F;
    op.sr = reg70 & 0x1F;
    op.rr = reg80 & 0x0F;
    op.sl = (reg80 >> 4) & 0x0F;
    op.ks = (reg60 >> 5) & 0x03;
    op.am = ((reg60 >> 7) & 0x01) != 0;
    op.ssg_enable = (reg90 & 0x08) != 0;
    op.ssg = reg90 & 0x07;
  }
  return true;
}

ParseResult parse_new(const uint8_t *data, size_t size,
                      const std::string &fallback_name) {
  if (size < 8 ||
      !std::equal(kFinsMagic.begin(), kFinsMagic.end(), data))
    return Error{"Not a FINS format"};

  uint16_t instrument_type =
      static_cast<uint16_t>(data[6] | (data[7] << 8));
  if (instrument_type != 1)
    return Error{"FUI instrument is not FM (OPN)"};

  Patch result{};
  result.left = true;
  result.right = true;

  size_t pos = 8;
  bool fm_loaded = false;
  bool name_loaded = false;

  while (pos + 4 <= size) {
    uint16_t feature_code =
        static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
    uint16_t feature_length =
        static_cast<uint16_t>(data[pos + 2] | (data[pos + 3] << 8));
    pos += 4;

    if (pos + feature_length > size)
      return Error{"FUI feature block overruns data"};

    char code_chars[3] = {static_cast<char>(feature_code & 0xFF),
                          static_cast<char>((feature_code >> 8) & 0xFF), '\0'};
    std::string feature_name(code_chars);

    if (feature_name == "NA") {
      auto end = std::find(data + pos, data + pos + feature_length,
                           static_cast<uint8_t>(0));
      result.name.assign(data + pos, end);
      name_loaded = true;
    } else if (feature_name == "FM") {
      fm_loaded = parse_fm_feature(data + pos, feature_length, result);
    } else if (feature_name == "EN") {
      break;
    }

    pos += feature_length;
  }

  if (!name_loaded)
    result.name = fallback_name;

  if (!fm_loaded)
    return Error{"FUI file has no FM feature block"};

  return ParseOk{{result}, {}};
}

ParseResult parse_old(const uint8_t *data, size_t size,
                      const std::string &fallback_name) {
  if (size < kOldMagic.size() + 8 ||
      !std::equal(kOldMagic.begin(), kOldMagic.end(), data))
    return Error{"Not a legacy FUI format"};

  uint32_t instrument_ptr = read_le<uint32_t>(data, size, 16 + 4);
  if (instrument_ptr >= size)
    return Error{"Legacy FUI header index is invalid"};

  size_t pos = instrument_ptr;
  if (pos + 8 > size || data[pos] != 'I' || data[pos + 1] != 'N' ||
      data[pos + 2] != 'S' || data[pos + 3] != 'T')
    return Error{"Legacy FUI is missing an INST block"};

  pos += 4;
  uint32_t block_size = read_le<uint32_t>(data, size, pos);
  pos += 4;
  size_t block_end =
      (block_size != 0 && instrument_ptr + 8u + block_size <= size)
          ? instrument_ptr + 8u + block_size
          : size;

  if (pos + 2 > block_end)
    return Error{"Legacy FUI instrument block is invalid"};

  uint16_t data_version = read_le<uint16_t>(data, size, pos);
  pos += 2;

  if (pos >= block_end)
    return Error{"Legacy FUI instrument block is too short"};

  uint8_t instrument_type = data[pos++];
  pos += 1; // reserved
  if (instrument_type != 1)
    return Error{"Legacy FUI instrument is not FM (OPN)"};

  // Read name
  size_t name_end = pos;
  while (name_end < block_end && data[name_end] != 0)
    ++name_end;
  std::string inst_name(data + pos, data + name_end);
  pos = (name_end < block_end) ? name_end + 1 : block_end;

  Patch result{};
  result.name = inst_name.empty() ? fallback_name : inst_name;
  result.left = true;
  result.right = true;

  if (pos + 8 > block_end)
    return Error{"Legacy FUI FM block lacks required data"};

  result.algorithm = data[pos++] & 0x07;
  result.feedback = data[pos++] & 0x07;
  result.fms = data[pos++] & 0x07;
  result.ams = data[pos++] & 0x03;

  pos += 1; // operator_count
  pos += 1; // OPLL preset / reserved
  pos += 2; // reserved

  for (int i = 0; i < 4; ++i) {
    if (pos + 20 > block_end)
      break;

    uint8_t am_val = data[pos++];
    uint8_t attack_rate = data[pos++];
    uint8_t decay_rate = data[pos++];
    uint8_t multiple = data[pos++];
    uint8_t release_rate = data[pos++];
    uint8_t sustain_level = data[pos++];
    uint8_t total_level = data[pos++];
    pos += 1; // dt2
    pos += 1; // rs (unused here — key_scale read separately)
    uint8_t detune_val = data[pos++];
    uint8_t sustain_rate = data[pos++];
    uint8_t ssg_env = data[pos++];
    pos += 3; // dam, dvb, egt
    uint8_t key_scale = data[pos++];
    pos += 3; // sus, vib, ws
    pos += 1; // ksr

    // Version-dependent fields
    pos += 1; // enable flag or reserved
    pos += 1; // kvs or reserved
    pos += 10; // reserved

    auto &op = result.operators[i];
    op.am = (am_val != 0);
    op.ar = std::min<uint8_t>(attack_rate, 31);
    op.dr = std::min<uint8_t>(decay_rate, 31);
    op.ml = std::min<uint8_t>(multiple, 15);
    op.rr = std::min<uint8_t>(release_rate, 15);
    op.sl = std::min<uint8_t>(sustain_level, 15);
    op.tl = std::min<uint8_t>(total_level, 127);
    op.dt = detune_from_linear(detune_val);
    op.sr = std::min<uint8_t>(sustain_rate, 31);
    op.ssg_enable = (ssg_env & 0x10) != 0;
    op.ssg = op.ssg_enable ? (ssg_env & 0x07) : 0;
    op.ks = key_scale & 0x03;
  }

  return ParseOk{{result}, {}};
}

} // namespace

ParseResult parse(const uint8_t *data, size_t size, const std::string &name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  // Try modern format first
  auto result = parse_new(data, size, name);
  if (is_ok(result))
    return result;

  // Fall back to legacy format
  return parse_old(data, size, name);
}

SerializeResult serialize(const Patch &patch) {
  std::vector<uint8_t> bytes;
  bytes.reserve(4 + 2 + 2 + 64);

  // Header: magic + version (0) + instrument type (1 = FM)
  bytes.insert(bytes.end(), kFinsMagic.begin(), kFinsMagic.end());
  uint16_t version = 0;
  uint16_t instrument_type = 1;
  bytes.push_back(static_cast<uint8_t>(version & 0xFF));
  bytes.push_back(static_cast<uint8_t>((version >> 8) & 0xFF));
  bytes.push_back(static_cast<uint8_t>(instrument_type & 0xFF));
  bytes.push_back(static_cast<uint8_t>((instrument_type >> 8) & 0xFF));

  auto write_feature = [&](const char code[2],
                           const std::vector<uint8_t> &payload) {
    bytes.push_back(static_cast<uint8_t>(code[0]));
    bytes.push_back(static_cast<uint8_t>(code[1]));
    uint16_t len = static_cast<uint16_t>(payload.size());
    bytes.push_back(static_cast<uint8_t>(len & 0xFF));
    bytes.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
  };

  // NA feature (name)
  {
    std::vector<uint8_t> name_block(patch.name.begin(), patch.name.end());
    name_block.push_back(0);
    const char code[2] = {'N', 'A'};
    write_feature(code, name_block);
  }

  // FM feature
  {
    std::vector<uint8_t> fm;
    fm.reserve(4 + 4 * 8);

    uint8_t flags = 0x04; // op count = 4
    flags |= (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7); // all ops enabled
    fm.push_back(flags);

    uint8_t alg_fb = static_cast<uint8_t>((patch.algorithm & 0x07) << 4 |
                                           (patch.feedback & 0x07));
    fm.push_back(alg_fb);

    uint8_t fms_ams = static_cast<uint8_t>((patch.fms & 0x07) |
                                            ((patch.ams & 0x03) << 3));
    fm.push_back(fms_ams);
    fm.push_back(0); // reserved

    for (int i = 0; i < 4; ++i) {
      const auto &op = patch.operators[i];
      uint8_t reg30 = static_cast<uint8_t>(
          (detune_to_linear(op.dt) & 0x0F) << 4 | (op.ml & 0x0F));
      uint8_t reg40 = static_cast<uint8_t>(op.tl & 0x7F);
      uint8_t reg50 = static_cast<uint8_t>(op.ar & 0x1F);
      uint8_t reg60 = static_cast<uint8_t>(
          (op.dr & 0x1F) | ((op.ks & 0x03) << 5) | (op.am ? 0x80 : 0x00));
      uint8_t reg70 = static_cast<uint8_t>(op.sr & 0x1F);
      uint8_t reg80 = static_cast<uint8_t>((op.rr & 0x0F) |
                                            ((op.sl & 0x0F) << 4));
      uint8_t reg90 =
          op.ssg_enable
              ? static_cast<uint8_t>(0x08 | (op.ssg & 0x07))
              : 0;
      uint8_t reg94 = 0;

      fm.push_back(reg30);
      fm.push_back(reg40);
      fm.push_back(reg50);
      fm.push_back(reg60);
      fm.push_back(reg70);
      fm.push_back(reg80);
      fm.push_back(reg90);
      fm.push_back(reg94);
    }

    const char code[2] = {'F', 'M'};
    write_feature(code, fm);
  }

  // EN feature (end marker)
  {
    const char code[2] = {'E', 'N'};
    write_feature(code, {});
  }

  return bytes;
}

} // namespace ym2612_format::fui
