#include "ym2612_format/fur.hpp"

#include "ym2612_format/fui.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <miniz.h>
#include <string>
#include <vector>

namespace ym2612_format::fur {

FormatInfo info() {
  return {Format::Fur, "Furnace Module", "fur", true, false, false};
}

namespace {

constexpr std::array<char, 16> kFurMagic{'-', 'F', 'u', 'r', 'n', 'a',
                                          'c', 'e', ' ', 'm', 'o', 'd',
                                          'u', 'l', 'e', '-'};

/// Simple binary reader with bounds checking.
struct Reader {
  const uint8_t *data;
  size_t size;
  size_t pos = 0;

  bool has(size_t n) const { return pos + n <= size; }
  bool eof() const { return pos >= size; }

  uint8_t u8() {
    if (pos >= size)
      return 0;
    return data[pos++];
  }

  uint16_t u16_le() {
    uint8_t b0 = u8();
    uint8_t b1 = u8();
    return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
  }

  uint32_t u32_le() {
    uint8_t b0 = u8();
    uint8_t b1 = u8();
    uint8_t b2 = u8();
    uint8_t b3 = u8();
    return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) |
           (static_cast<uint32_t>(b2) << 16) |
           (static_cast<uint32_t>(b3) << 24);
  }

  float f32_le() {
    uint32_t bits = u32_le();
    float val;
    std::memcpy(&val, &bits, sizeof(float));
    return val;
  }

  /// Read a null-terminated string.
  std::string read_str() {
    std::string s;
    while (pos < size) {
      uint8_t c = data[pos++];
      if (c == 0)
        break;
      s.push_back(static_cast<char>(c));
    }
    return s;
  }

  void skip(size_t n) { pos = std::min(pos + n, size); }
  void seek(size_t p) { pos = std::min(p, size); }
};

/// Convert an INS2 instrument block into FINS-compatible data that can be
/// passed to fui::parse().
std::vector<uint8_t> ins2_to_fins(const uint8_t *block_data, size_t block_size,
                                  uint16_t format_version,
                                  uint16_t instrument_type) {
  // FINS header: "FINS" + version(2) + type(2)
  std::vector<uint8_t> fins;
  fins.reserve(8 + block_size);

  fins.push_back('F');
  fins.push_back('I');
  fins.push_back('N');
  fins.push_back('S');
  fins.push_back(static_cast<uint8_t>(format_version & 0xFF));
  fins.push_back(static_cast<uint8_t>((format_version >> 8) & 0xFF));
  fins.push_back(static_cast<uint8_t>(instrument_type & 0xFF));
  fins.push_back(static_cast<uint8_t>((instrument_type >> 8) & 0xFF));

  // Copy feature blocks directly
  fins.insert(fins.end(), block_data, block_data + block_size);

  return fins;
}

/// Try to decompress zlib data, returning the decompressed buffer.
/// If decompression fails, returns an empty vector.
std::vector<uint8_t> try_decompress(const uint8_t *data, size_t size) {
  std::vector<uint8_t> decompressed;
  mz_ulong out_len = std::max<mz_ulong>(size * 8, 1024 * 256);
  for (int attempt = 0; attempt < 10; ++attempt) {
    decompressed.resize(out_len);
    int ret = mz_uncompress(decompressed.data(), &out_len, data, size);
    if (ret == MZ_OK) {
      decompressed.resize(out_len);
      return decompressed;
    } else if (ret == MZ_BUF_ERROR) {
      out_len *= 2;
      continue;
    } else {
      return {};
    }
  }
  return {};
}

/// Parse instruments from the new-style INF2 block (version >= 240).
bool parse_inf2(Reader &r, std::vector<Patch> &patches,
                std::vector<std::string> &warnings) {
  // Skip past song info strings and chip definitions
  // INF2 block: we've already passed the block header

  // Song info strings
  r.read_str(); // song name
  r.read_str(); // song author
  r.read_str(); // system name
  r.read_str(); // album/category/game name
  r.read_str(); // song name (Japanese)
  r.read_str(); // song author (Japanese)
  r.read_str(); // system name (Japanese)
  r.read_str(); // album/category/game name (Japanese)

  r.f32_le(); // A-4 tuning
  r.u8();     // automatic system name

  // System definition
  r.f32_le();             // master volume
  uint16_t channels = r.u16_le();
  uint16_t num_chips = r.u16_le();

  // Chip definitions
  for (uint16_t i = 0; i < num_chips; ++i) {
    r.u16_le(); // chip ID
    r.u16_le(); // chip channel count
    r.f32_le(); // chip volume
    r.f32_le(); // chip panning
    r.f32_le(); // chip front/rear balance
  }

  // Patchbay
  uint32_t patchbay_count = r.u32_le();
  r.skip(patchbay_count * 4);
  r.u8(); // automatic patchbay

  // Song elements
  while (!r.eof()) {
    uint8_t element_type = r.u8();
    if (element_type == 0)
      break; // end of element list

    uint32_t num_elements = r.u32_le();
    std::vector<uint32_t> pointers;
    pointers.reserve(num_elements);
    for (uint32_t i = 0; i < num_elements; ++i)
      pointers.push_back(r.u32_le());

    if (element_type == 0x04) {
      // INS2 instruments
      for (uint32_t ptr : pointers) {
        if (ptr + 12 > r.size)
          continue;

        // Verify INS2 block ID
        if (r.data[ptr] != 'I' || r.data[ptr + 1] != 'N' ||
            r.data[ptr + 2] != 'S' || r.data[ptr + 3] != '2')
          continue;

        uint32_t block_size =
            static_cast<uint32_t>(r.data[ptr + 4]) |
            (static_cast<uint32_t>(r.data[ptr + 5]) << 8) |
            (static_cast<uint32_t>(r.data[ptr + 6]) << 16) |
            (static_cast<uint32_t>(r.data[ptr + 7]) << 24);

        size_t block_start = ptr + 8;
        if (block_start + 4 > r.size)
          continue;

        uint16_t fmt_ver =
            static_cast<uint16_t>(r.data[block_start]) |
            (static_cast<uint16_t>(r.data[block_start + 1]) << 8);
        uint16_t inst_type =
            static_cast<uint16_t>(r.data[block_start + 2]) |
            (static_cast<uint16_t>(r.data[block_start + 3]) << 8);

        // Only extract FM (OPN) instruments — type 1
        if (inst_type != 1)
          continue;

        // Feature blocks start after format_version(2) + instrument_type(2)
        size_t features_offset = block_start + 4;
        size_t features_size =
            (block_size >= 4) ? block_size - 4 : 0;
        if (features_offset + features_size > r.size)
          features_size = r.size - features_offset;

        auto fins_data = ins2_to_fins(r.data + features_offset, features_size,
                                      fmt_ver, inst_type);
        auto result = fui::parse(fins_data.data(), fins_data.size(), "");
        if (is_ok(result)) {
          for (auto &p : get_ok(result).patches)
            patches.push_back(std::move(p));
        }
      }
    }
    // Skip other element types
  }

  return true;
}

/// Parse instruments from the old-style INFO block (version < 240, >= 127).
bool parse_info_old(Reader &r, uint16_t fur_version,
                    std::vector<Patch> &patches,
                    std::vector<std::string> &warnings) {
  // Old INFO block
  r.u8();     // time base
  r.u8();     // speed 1
  r.u8();     // speed 2
  r.u8();     // initial arpeggio time
  r.f32_le(); // ticks per second
  uint16_t pattern_len = r.u16_le();
  uint16_t orders_len = r.u16_le();
  r.u8(); // highlight A
  r.u8(); // highlight B

  uint16_t instrument_count = r.u16_le();
  uint16_t wavetable_count = r.u16_le();
  uint16_t sample_count = r.u16_le();
  uint32_t pattern_count = r.u32_le();

  // Sound chip IDs (32 bytes)
  uint8_t chip_ids[32];
  int num_chips = 0;
  uint16_t total_channels = 0;
  for (int i = 0; i < 32; ++i) {
    chip_ids[i] = r.u8();
    if (chip_ids[i] != 0)
      num_chips = i + 1;
  }

  // Count channels based on chip IDs
  // For simplicity, use a basic mapping
  for (int i = 0; i < num_chips; ++i) {
    uint8_t cid = chip_ids[i];
    if (cid == 0)
      break;
    // Rough channel count mapping for common chips
    switch (cid) {
    case 0x83: total_channels += 6; break;   // YM2612
    case 0xa0: total_channels += 9; break;   // YM2612 ext
    case 0xbd: total_channels += 11; break;  // YM2612 DualPCM ext
    case 0xbe: total_channels += 7; break;   // YM2612 DualPCM
    case 0xc1: total_channels += 10; break;  // YM2612 CSM
    case 0x03: total_channels += 4; break;   // SN76489
    case 0x04: total_channels += 4; break;   // Game Boy
    case 0x06: total_channels += 5; break;   // NES
    case 0x82: total_channels += 8; break;   // YM2151
    case 0x8d: total_channels += 6; break;   // YM2203
    case 0xb6: total_channels += 9; break;   // YM2203 ext
    case 0x8e: total_channels += 16; break;  // YM2608
    case 0xb7: total_channels += 19; break;  // YM2608 ext
    case 0xa5: total_channels += 14; break;  // YM2610
    case 0xa6: total_channels += 17; break;  // YM2610 ext
    case 0x9e: total_channels += 16; break;  // YM2610B
    case 0x80: total_channels += 3; break;   // AY-3-8910
    case 0x81: total_channels += 4; break;   // Amiga
    case 0x05: total_channels += 6; break;   // PC Engine
    case 0x89: total_channels += 9; break;   // OPLL
    case 0x9b: total_channels += 16; break;  // SegaPCM
    case 0x07: case 0x47: total_channels += 3; break; // C64
    // Compound IDs (legacy)
    case 0x02: total_channels += 10; break;  // Genesis
    case 0x42: total_channels += 13; break;  // Genesis ext
    case 0x08: total_channels += 13; break;  // Arcade
    case 0x09: total_channels += 13; break;  // Neo Geo CD
    case 0x49: total_channels += 16; break;  // Neo Geo CD ext
    default: total_channels += 8; break;     // fallback
    }
  }

  r.skip(32); // chip volumes (reserved)
  r.skip(32); // chip panning (reserved)
  r.skip(128); // chip flag pointers / flags

  // Strings
  r.read_str(); // song name
  r.read_str(); // song author

  r.f32_le(); // A-4 tuning

  // Compatibility flags — all are "or reserved" meaning always present.
  // 20 bytes total (limit slides through reset note base):
  //   >=36: limit_slides(1), linear_pitch(1), loop_modality(1)
  //   >=42: proper_noise(1), wave_duty(1)
  //   >=45: reset_macro(1), legacy_vol(1), compat_arp(1),
  //         note_off_resets(1), target_resets(1)
  //   >=47: arp_inhibit(1), wack_algo(1)
  //   >=49: broken_shortcut(1)
  //   >=50: ignore_dup(1)
  //   >=62: stop_porta(1), continuous_vibrato(1)
  //   >=64: broken_dac(1)
  //   >=65: one_tick_cut(1)
  //   >=66: inst_change_porta(1)
  //   >=69: reset_note_base(1)
  r.skip(20);

  // Instrument pointers
  std::vector<uint32_t> ins_ptrs;
  ins_ptrs.reserve(instrument_count);
  for (uint16_t i = 0; i < instrument_count; ++i)
    ins_ptrs.push_back(r.u32_le());

  // Skip wavetable pointers
  r.skip(static_cast<size_t>(wavetable_count) * 4);
  // Skip sample pointers
  r.skip(static_cast<size_t>(sample_count) * 4);
  // Skip pattern pointers
  r.skip(static_cast<size_t>(pattern_count) * 4);

  // Now parse each instrument
  for (uint32_t ptr : ins_ptrs) {
    if (ptr + 12 > r.size)
      continue;

    // Check block ID
    if (r.data[ptr] == 'I' && r.data[ptr + 1] == 'N' &&
        r.data[ptr + 2] == 'S' && r.data[ptr + 3] == '2') {
      // New format INS2 block
      uint32_t block_size =
          static_cast<uint32_t>(r.data[ptr + 4]) |
          (static_cast<uint32_t>(r.data[ptr + 5]) << 8) |
          (static_cast<uint32_t>(r.data[ptr + 6]) << 16) |
          (static_cast<uint32_t>(r.data[ptr + 7]) << 24);

      size_t block_start = ptr + 8;
      if (block_start + 4 > r.size)
        continue;

      uint16_t fmt_ver =
          static_cast<uint16_t>(r.data[block_start]) |
          (static_cast<uint16_t>(r.data[block_start + 1]) << 8);
      uint16_t inst_type =
          static_cast<uint16_t>(r.data[block_start + 2]) |
          (static_cast<uint16_t>(r.data[block_start + 3]) << 8);

      if (inst_type != 1)
        continue;

      size_t features_offset = block_start + 4;
      size_t features_size =
          (block_size >= 4) ? block_size - 4 : 0;
      if (features_offset + features_size > r.size)
        features_size = r.size - features_offset;

      auto fins_data = ins2_to_fins(r.data + features_offset, features_size,
                                    fmt_ver, inst_type);
      auto result = fui::parse(fins_data.data(), fins_data.size(), "");
      if (is_ok(result)) {
        for (auto &p : get_ok(result).patches)
          patches.push_back(std::move(p));
      }
    }
    // Skip old-format instruments (< 127)
  }

  return true;
}

} // namespace

ParseResult parse(const uint8_t *data, size_t size, const std::string &name) {
  if (!data || size < 4)
    return Error{"Data too small for FUR file"};

  // FUR files may be zlib-compressed (entire file).
  // Try decompressing first, then look for the header.
  const uint8_t *file_data = data;
  size_t file_size = size;
  std::vector<uint8_t> decompressed;

  if (!std::equal(kFurMagic.begin(), kFurMagic.end(), data)) {
    // Not an uncompressed FUR — try decompressing the whole file
    decompressed = try_decompress(data, size);
    if (decompressed.empty())
      return Error{"Not a Furnace module file"};
    file_data = decompressed.data();
    file_size = decompressed.size();
  }

  if (file_size < 32)
    return Error{"Data too small for FUR header"};

  // Verify magic
  if (!std::equal(kFurMagic.begin(), kFurMagic.end(), file_data))
    return Error{"Not a Furnace module file"};

  uint16_t fur_version =
      static_cast<uint16_t>(file_data[16]) |
      (static_cast<uint16_t>(file_data[17]) << 8);

  // We only support version >= 127 (new instrument format)
  if (fur_version < 127)
    return Error{"FUR version " + std::to_string(fur_version) +
                 " is too old (need >= 127)"};

  uint32_t info_ptr =
      static_cast<uint32_t>(file_data[20]) |
      (static_cast<uint32_t>(file_data[21]) << 8) |
      (static_cast<uint32_t>(file_data[22]) << 16) |
      (static_cast<uint32_t>(file_data[23]) << 24);

  if (info_ptr + 8 > file_size)
    return Error{"FUR info pointer out of range"};

  // Read block ID at info_ptr
  Reader r{file_data, file_size, info_ptr};

  char block_id[5] = {};
  for (int i = 0; i < 4; ++i)
    block_id[i] = static_cast<char>(r.u8());
  uint32_t block_size = r.u32_le();

  std::vector<Patch> patches;
  std::vector<std::string> warnings;

  if (std::string(block_id) == "INF2") {
    // New format (>= 240)
    parse_inf2(r, patches, warnings);
  } else if (std::string(block_id) == "INFO") {
    // Old format (< 240 but >= 127)
    parse_info_old(r, fur_version, patches, warnings);
  } else {
    return Error{"FUR file has unknown info block: " + std::string(block_id)};
  }

  if (patches.empty())
    return Error{"No FM (OPN) instruments found in FUR module"};

  // Assign fallback names to unnamed patches
  for (size_t i = 0; i < patches.size(); ++i) {
    if (patches[i].name.empty()) {
      std::string base = name.empty() ? "instrument" : name;
      patches[i].name = base + "_" + std::to_string(i);
    }
  }

  return ParseOk{std::move(patches), std::move(warnings)};
}

} // namespace ym2612_format::fur
