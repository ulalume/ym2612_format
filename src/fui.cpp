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

// ---- FM feature ----

bool parse_fm_feature(const uint8_t *data, size_t size, Patch &result,
                      uint16_t format_version) {
  if (size < 4)
    return false;

  const uint8_t flags = data[0];
  const uint8_t op_count =
      std::min<uint8_t>((flags & 0x0F) ? (flags & 0x0F) : 4, 4);

  result.algorithm = (data[1] >> 4) & 0x07;
  result.feedback = data[1] & 0x07;
  result.fms = data[2] & 0x07;
  result.ams = (data[2] >> 3) & 0x03;

  // Header is 4 bytes normally, 5 bytes for format version >= 224
  // (extra "Block" byte at position 4)
  size_t offset = (format_version >= 224) ? 5 : 4;
  constexpr size_t stride = 8;

  for (uint8_t i = 0; i < op_count; ++i, offset += stride) {
    if (offset + stride > size)
      return false;

    auto &op = result.operators[i];
    // FINS FM operator byte layout:
    //   byte 0: |KSR| DT | MULT |
    //   byte 1: |SUS|    TL     |
    //   byte 2: | RS |VIB|  AR  |     ← RS (= KS) at bits 6-7
    //   byte 3: |AM |KSL|  DR  |     ← AM at bit 7, KSL (OPL) at bits 5-6
    //   byte 4: |EGT|KVS|  D2R |     ← D2R = SR
    //   byte 5: | SL  |   RR   |
    //   byte 6: | DVB  |  SSG  |
    //   byte 7: |DAM|DT2|  WS  |
    const uint8_t b0 = data[offset + 0];
    const uint8_t b1 = data[offset + 1];
    const uint8_t b2 = data[offset + 2];
    const uint8_t b3 = data[offset + 3];
    const uint8_t b4 = data[offset + 4];
    const uint8_t b5 = data[offset + 5];
    const uint8_t b6 = data[offset + 6];

    op.dt = detune_from_linear((b0 >> 4) & 0x07);
    op.ml = b0 & 0x0F;
    op.tl = b1 & 0x7F;
    op.ks = (b2 >> 6) & 0x03;       // RS from byte 2 bits 6-7
    op.ar = b2 & 0x1F;
    op.am = ((b3 >> 7) & 0x01) != 0; // AM from byte 3 bit 7
    op.dr = b3 & 0x1F;
    op.sr = b4 & 0x1F;               // D2R = SR
    op.sl = (b5 >> 4) & 0x0F;
    op.rr = b5 & 0x0F;
    op.ssg_enable = (b6 & 0x08) != 0;
    op.ssg = b6 & 0x07;
  }
  return true;
}

// ---- Macro feature parsing ----

/// Read a single Macro entry from a MA/O1-O4 block stream.
/// Returns the number of bytes consumed, or 0 on error.
size_t read_one_macro(const uint8_t *data, size_t size, size_t pos,
                      uint8_t &code_out, Macro &macro_out) {
  if (pos >= size)
    return 0;
  code_out = data[pos];
  if (code_out == 255)
    return 1; // end marker

  if (pos + 8 > size)
    return 0;

  uint8_t length = data[pos + 1];
  uint8_t loop = data[pos + 2];
  uint8_t release = data[pos + 3];
  uint8_t mode = data[pos + 4];
  uint8_t type_byte = data[pos + 5];
  uint8_t delay = data[pos + 6];
  uint8_t speed = data[pos + 7];

  uint8_t word_size_code = (type_byte >> 6) & 0x03;
  uint8_t macro_type = (type_byte >> 1) & 0x03;

  size_t bytes_per_val = 1;
  switch (word_size_code) {
  case 2: bytes_per_val = 2; break;
  case 3: bytes_per_val = 4; break;
  default: break;
  }

  size_t data_bytes = static_cast<size_t>(length) * bytes_per_val;
  if (pos + 8 + data_bytes > size)
    return 0;

  Macro m;
  m.loop = loop;
  m.release = release;
  m.type = static_cast<MacroType>(macro_type);
  m.delay = delay;
  m.speed = speed;
  m.values.reserve(length);

  size_t vpos = pos + 8;
  for (uint8_t i = 0; i < length; ++i) {
    int32_t val = 0;
    switch (word_size_code) {
    case 0: val = data[vpos]; vpos += 1; break;
    case 1: val = static_cast<int8_t>(data[vpos]); vpos += 1; break;
    case 2:
      val = static_cast<int16_t>(
          static_cast<uint16_t>(data[vpos] | (data[vpos + 1] << 8)));
      vpos += 2;
      break;
    case 3:
      val = static_cast<int32_t>(
          static_cast<uint32_t>(data[vpos] | (data[vpos + 1] << 8) |
                                (data[vpos + 2] << 16) |
                                (data[vpos + 3] << 24)));
      vpos += 4;
      break;
    }
    m.values.push_back(val);
  }

  macro_out = std::move(m);
  return 8 + data_bytes;
}

bool parse_ma_feature(const uint8_t *data, size_t size,
                      ChannelMacros &macros) {
  size_t pos = 0;
  while (pos < size) {
    uint8_t code;
    Macro m;
    size_t consumed = read_one_macro(data, size, pos, code, m);
    if (consumed == 0)
      break;
    pos += consumed;
    if (code == 255)
      break;

    Macro *target = nullptr;
    switch (code) {
    case 0:  target = &macros.volume; break;
    case 1:  target = &macros.arpeggio; break;
    case 2:  target = &macros.duty; break;
    case 3:  target = &macros.wave; break;
    case 4:  target = &macros.pitch; break;
    case 5:  target = &macros.ex1; break;
    case 6:  target = &macros.ex2; break;
    case 7:  target = &macros.ex3; break;
    case 8:  target = &macros.algorithm; break;
    case 9:  target = &macros.feedback; break;
    case 10: target = &macros.fms; break;
    case 11: target = &macros.ams; break;
    case 12: target = &macros.pan_left; break;
    case 13: target = &macros.pan_right; break;
    case 14: target = &macros.phase_reset; break;
    default: break;
    }
    if (target)
      *target = std::move(m);
  }
  return true;
}

bool parse_op_macro_feature(const uint8_t *data, size_t size,
                            OperatorMacros &op_macros) {
  size_t pos = 0;
  while (pos < size) {
    uint8_t code;
    Macro m;
    size_t consumed = read_one_macro(data, size, pos, code, m);
    if (consumed == 0)
      break;
    pos += consumed;
    if (code == 255)
      break;

    Macro *target = nullptr;
    switch (code) {
    case 0:  target = &op_macros.tl; break;
    case 1:  target = &op_macros.ar; break;
    case 2:  target = &op_macros.dr; break;
    case 3:  target = &op_macros.d2r; break;
    case 4:  target = &op_macros.rr; break;
    case 5:  target = &op_macros.sl; break;
    case 6:  target = &op_macros.dt; break;
    case 7:  target = &op_macros.ml; break;
    case 8:  target = &op_macros.rs; break;
    case 9:  target = &op_macros.ssg; break;
    case 10: target = &op_macros.am; break;
    default: break;
    }
    if (target)
      *target = std::move(m);
  }
  return true;
}

// ---- Macro serialization ----

/// Determine the minimal word size code for a macro's values.
uint8_t macro_word_size(const Macro &m) {
  bool need_signed = false;
  bool need_16 = false;
  bool need_32 = false;
  for (int32_t v : m.values) {
    if (v < 0)
      need_signed = true;
    if (v < -128 || v > 127)
      need_16 = true;
    if (v < -32768 || v > 32767)
      need_32 = true;
  }
  if (need_32)
    return 3; // i32
  if (need_16)
    return 2; // i16
  if (need_signed)
    return 1; // i8
  return 0;   // u8
}

void write_one_macro(std::vector<uint8_t> &out, uint8_t code,
                     const Macro &m) {
  if (m.empty())
    return;

  uint8_t ws = macro_word_size(m);
  uint8_t type_byte = static_cast<uint8_t>((ws << 6) |
      (static_cast<uint8_t>(m.type) << 1));

  out.push_back(code);
  out.push_back(static_cast<uint8_t>(m.values.size()));
  out.push_back(m.loop);
  out.push_back(m.release);
  out.push_back(0); // mode
  out.push_back(type_byte);
  out.push_back(m.delay);
  out.push_back(m.speed);

  for (int32_t v : m.values) {
    switch (ws) {
    case 0:
      out.push_back(static_cast<uint8_t>(v));
      break;
    case 1:
      out.push_back(static_cast<uint8_t>(static_cast<int8_t>(v)));
      break;
    case 2: {
      auto u = static_cast<uint16_t>(static_cast<int16_t>(v));
      out.push_back(static_cast<uint8_t>(u & 0xFF));
      out.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
      break;
    }
    case 3: {
      auto u = static_cast<uint32_t>(v);
      out.push_back(static_cast<uint8_t>(u & 0xFF));
      out.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
      out.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
      out.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
      break;
    }
    }
  }
}

std::vector<uint8_t> serialize_ma(const ChannelMacros &macros) {
  std::vector<uint8_t> out;
  write_one_macro(out, 0, macros.volume);
  write_one_macro(out, 1, macros.arpeggio);
  write_one_macro(out, 2, macros.duty);
  write_one_macro(out, 3, macros.wave);
  write_one_macro(out, 4, macros.pitch);
  write_one_macro(out, 5, macros.ex1);
  write_one_macro(out, 6, macros.ex2);
  write_one_macro(out, 7, macros.ex3);
  write_one_macro(out, 8, macros.algorithm);
  write_one_macro(out, 9, macros.feedback);
  write_one_macro(out, 10, macros.fms);
  write_one_macro(out, 11, macros.ams);
  write_one_macro(out, 12, macros.pan_left);
  write_one_macro(out, 13, macros.pan_right);
  write_one_macro(out, 14, macros.phase_reset);
  out.push_back(255); // end marker
  return out;
}

std::vector<uint8_t> serialize_op_macro(const OperatorMacros &om) {
  std::vector<uint8_t> out;
  write_one_macro(out, 0, om.tl);
  write_one_macro(out, 1, om.ar);
  write_one_macro(out, 2, om.dr);
  write_one_macro(out, 3, om.d2r);
  write_one_macro(out, 4, om.rr);
  write_one_macro(out, 5, om.sl);
  write_one_macro(out, 6, om.dt);
  write_one_macro(out, 7, om.ml);
  write_one_macro(out, 8, om.rs);
  write_one_macro(out, 9, om.ssg);
  write_one_macro(out, 10, om.am);
  out.push_back(255);
  return out;
}

// ---- FINS parsers ----

ParseResult parse_new(const uint8_t *data, size_t size,
                      const std::string &fallback_name) {
  if (size < 8 ||
      !std::equal(kFinsMagic.begin(), kFinsMagic.end(), data))
    return Error{"Not a FINS format"};

  uint16_t format_version =
      static_cast<uint16_t>(data[4] | (data[5] << 8));
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
      fm_loaded =
          parse_fm_feature(data + pos, feature_length, result, format_version);
    } else if (feature_name == "MA") {
      parse_ma_feature(data + pos, feature_length, result.macros);
    } else if (feature_name == "O1") {
      parse_op_macro_feature(data + pos, feature_length,
                             result.operator_macros[0]);
    } else if (feature_name == "O2") {
      parse_op_macro_feature(data + pos, feature_length,
                             result.operator_macros[1]);
    } else if (feature_name == "O3") {
      parse_op_macro_feature(data + pos, feature_length,
                             result.operator_macros[2]);
    } else if (feature_name == "O4") {
      parse_op_macro_feature(data + pos, feature_length,
                             result.operator_macros[3]);
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
  bytes.reserve(4 + 2 + 2 + 128);

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
      // FINS FM operator byte layout:
      //   byte 0: |KSR| DT | MULT |
      //   byte 1: |SUS|    TL     |
      //   byte 2: | RS |VIB|  AR  |     ← RS (= KS) at bits 6-7
      //   byte 3: |AM |KSL|  DR  |     ← AM at bit 7
      //   byte 4: |EGT|KVS|  D2R |     ← D2R = SR
      //   byte 5: | SL  |   RR   |
      //   byte 6: | DVB  |  SSG  |
      //   byte 7: |DAM|DT2|  WS  |
      uint8_t b0 = static_cast<uint8_t>(
          ((detune_to_linear(op.dt) & 0x07) << 4) | (op.ml & 0x0F));
      uint8_t b1 = static_cast<uint8_t>(op.tl & 0x7F);
      uint8_t b2 = static_cast<uint8_t>(
          ((op.ks & 0x03) << 6) | (op.ar & 0x1F));
      uint8_t b3 = static_cast<uint8_t>(
          (op.am ? 0x80 : 0x00) | (op.dr & 0x1F));
      uint8_t b4 = static_cast<uint8_t>(op.sr & 0x1F);
      uint8_t b5 = static_cast<uint8_t>(
          ((op.sl & 0x0F) << 4) | (op.rr & 0x0F));
      uint8_t b6 = op.ssg_enable
          ? static_cast<uint8_t>(0x08 | (op.ssg & 0x07))
          : 0;
      uint8_t b7 = 0;

      fm.push_back(b0);
      fm.push_back(b1);
      fm.push_back(b2);
      fm.push_back(b3);
      fm.push_back(b4);
      fm.push_back(b5);
      fm.push_back(b6);
      fm.push_back(b7);
    }

    const char code[2] = {'F', 'M'};
    write_feature(code, fm);
  }

  // MA feature (channel macros) — only if macros present
  if (!patch.macros.empty()) {
    auto payload = serialize_ma(patch.macros);
    const char code[2] = {'M', 'A'};
    write_feature(code, payload);
  }

  // O1-O4 features (per-operator macros)
  {
    const char codes[4][2] = {{'O', '1'}, {'O', '2'}, {'O', '3'}, {'O', '4'}};
    for (int i = 0; i < 4; ++i) {
      if (!patch.operator_macros[i].empty()) {
        auto payload = serialize_op_macro(patch.operator_macros[i]);
        write_feature(codes[i], payload);
      }
    }
  }

  // EN feature (end marker)
  {
    const char code[2] = {'E', 'N'};
    write_feature(code, {});
  }

  return bytes;
}

} // namespace ym2612_format::fui
