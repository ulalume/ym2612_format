#include "ym2612_format/dmf.hpp"

#include "ym2612_format/detune.hpp"
#include "ym2612_format/patch.hpp"
#include <algorithm>
#include <cstring>
#include <miniz.h>
#include <string>
#include <vector>

namespace ym2612_format::dmf {

FormatInfo info() {
  return {Format::Dmf, "DefleMask Module", "dmf", true, false, false};
}

namespace {

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

  uint32_t u32_le() {
    uint8_t b0 = u8();
    uint8_t b1 = u8();
    uint8_t b2 = u8();
    uint8_t b3 = u8();
    return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) |
           (static_cast<uint32_t>(b2) << 16) |
           (static_cast<uint32_t>(b3) << 24);
  }

  std::string read_string() {
    uint8_t len = u8();
    std::string s;
    s.reserve(len);
    for (int i = 0; i < len && !eof(); ++i)
      s.push_back(static_cast<char>(u8()));
    return s;
  }

  void skip(size_t n) { pos = std::min(pos + n, size); }
};

int system_total_channels(uint8_t sys) {
  uint8_t base = sys & 0x0F;
  // Mode bits: v0x15 uses bit 4, v0x18 uses bit 6.
  // Check both to handle all versions.
  bool ext_mode = (sys & 0x10) || (sys & 0x40);

  switch (base) {
  case 0x02: // GENESIS
    return ext_mode ? 13 : 10;
  case 0x03: // SMS
    return 4;
  case 0x04: // GAMEBOY
    return 4;
  case 0x05: // PCENGINE
    return 6;
  case 0x06: // NES
    return 5;
  case 0x07: // C64
    return 3;
  case 0x08: // ARCADE / YM2151
    return 13;
  case 0x09: // NEOGEO
    return ext_mode ? 16 : 13;
  default:
    return 0;
  }
}

bool is_gameboy(uint8_t sys) { return (sys & 0x0F) == 0x04; }

bool is_c64(uint8_t sys) { return (sys & 0x0F) == 0x07; }

/// Format version as "0x" + 2-digit hex string.
std::string hex_version(uint8_t v) {
  const char hex[] = "0123456789abcdef";
  std::string s = "0x";
  s += hex[(v >> 4) & 0x0F];
  s += hex[v & 0x0F];
  return s;
}

} // namespace

ParseResult parse(const uint8_t *data, size_t size, const std::string &name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  // DMF files are zlib-compressed.  Try to decompress.
  std::vector<uint8_t> decompressed;
  {
    mz_ulong out_len = std::max<mz_ulong>(size * 8, 1024 * 256);
    for (int attempt = 0; attempt < 8; ++attempt) {
      decompressed.resize(out_len);
      int ret = mz_uncompress(decompressed.data(), &out_len, data, size);
      if (ret == MZ_OK) {
        decompressed.resize(out_len);
        break;
      } else if (ret == MZ_BUF_ERROR) {
        out_len *= 2;
        continue;
      } else {
        return Error{"Failed to decompress DMF data (zlib error " +
                     std::to_string(ret) + ")"};
      }
    }
  }

  Reader r{decompressed.data(), decompressed.size()};
  std::vector<std::string> warnings;

  // FORMAT FLAGS
  if (!r.has(17))
    return Error{"DMF data too small"};

  char magic[16];
  for (int i = 0; i < 16; ++i)
    magic[i] = static_cast<char>(r.u8());
  if (std::memcmp(magic, ".DelekDefleMask.", 16) != 0)
    return Error{"Not a DMF file (invalid magic)"};

  uint8_t file_version = r.u8();

  if (file_version < 0x12 || file_version > 0x18) {
    warnings.push_back("Untested DMF version " + hex_version(file_version) +
                        ", attempting best-effort parse");
  }

  // SYSTEM SET
  uint8_t system = r.u8();
  int total_channels = system_total_channels(system);
  if (total_channels == 0) {
    warnings.push_back("Unknown system " + hex_version(system) +
                        ", guessing 10 channels");
    total_channels = 10;
  }

  // VISUAL INFORMATION
  r.read_string(); // song name
  r.read_string(); // song author
  r.skip(2);       // highlight A, highlight B

  // MODULE INFORMATION
  r.skip(1); // time base
  r.skip(1); // tick time 1
  r.skip(1); // tick time 2
  r.skip(1); // frames mode
  r.skip(1); // using custom HZ
  r.skip(3); // custom HZ (3 bytes)

  // TOTAL_ROWS_PER_PATTERN: 4 bytes in v0x18+, 1 byte in earlier versions
  if (file_version >= 0x18) {
    r.u32_le(); // total rows per pattern (unused for instrument extraction)
  } else {
    r.u8();
  }

  uint8_t total_rows_in_matrix = r.u8();

  // Arpeggio tick speed: present in v0x13 and earlier, removed in v0x14+.
  if (file_version <= 0x13)
    r.skip(1);

  // PATTERN MATRIX
  size_t matrix_bytes =
      static_cast<size_t>(total_channels) * total_rows_in_matrix;
  r.skip(matrix_bytes);

  // INSTRUMENTS DATA
  if (r.eof())
    return Error{"DMF truncated before instruments section"};

  uint8_t total_instruments = r.u8();
  std::vector<Patch> patches;

  for (int inst = 0; inst < total_instruments; ++inst) {
    if (r.eof()) {
      warnings.push_back("DMF truncated at instrument " +
                          std::to_string(inst));
      break;
    }

    std::string inst_name = r.read_string();
    uint8_t inst_mode = r.u8(); // 0 = STD, 1 = FM

    if (inst_mode == 1) {
      // FM INSTRUMENT
      Patch patch;
      patch.name = inst_name.empty()
                       ? (name.empty() ? "inst_" + std::to_string(inst)
                                       : name + "_" + std::to_string(inst))
                       : inst_name;
      patch.dac_enable = false;
      patch.lfo_enable = false;
      patch.lfo_frequency = 0;
      patch.left = true;
      patch.right = true;

      // FM header: v<0x13 has padding bytes between fields
      bool old_fmt = (file_version < 0x13);

      patch.algorithm = r.u8() & 0x07;
      if (old_fmt)
        r.u8(); // padding
      patch.feedback = r.u8() & 0x07;
      if (old_fmt)
        r.u8(); // padding
      patch.fms = r.u8() & 0x07;
      if (old_fmt) {
        r.u8(); // padding
        r.u8(); // ops count (always 4 for Genesis)
      }
      patch.ams = r.u8() & 0x03;

      for (int op = 0; op < 4; ++op) {
        auto &o = patch.operators[op];

        uint8_t am_val = r.u8();
        uint8_t ar = r.u8();
        if (old_fmt)
          r.u8(); // DAM
        uint8_t dr = r.u8();
        if (old_fmt) {
          r.u8(); // DVB
          r.u8(); // EGT
          r.u8(); // KSL
        }
        uint8_t mult = r.u8();
        uint8_t rr = r.u8();
        uint8_t sl = r.u8();
        if (old_fmt)
          r.u8(); // SUS
        uint8_t tl = r.u8();
        if (old_fmt) {
          r.u8(); // VIB
          r.u8(); // WS
        } else {
          r.u8(); // DT2 (YM2151-only)
        }
        uint8_t rs = r.u8();
        uint8_t dt = r.u8(); // hardware register encoding
        uint8_t d2r = r.u8();
        uint8_t ssgmode = r.u8();

        o.am = (am_val != 0);
        o.ar = ar & 0x1F;
        o.dr = dr & 0x1F;
        o.ml = mult & 0x0F;
        o.rr = rr & 0x0F;
        o.sl = sl & 0x0F;
        o.tl = tl & 0x7F;
        o.ks = rs & 0x03;
        o.dt = detune_from_linear(dt);
        o.sr = d2r & 0x1F;
        o.ssg_enable = (ssgmode & 0x08) != 0;
        o.ssg = ssgmode & 0x07;
        o.enable = true;
      }

      patches.push_back(std::move(patch));

    } else {
      // STANDARD INSTRUMENT — skip over it
      // Volume macro (not present for GameBoy)
      if (!is_gameboy(system)) {
        uint8_t env_size = r.u8();
        r.skip(static_cast<size_t>(env_size) * 4);
        if (env_size > 0)
          r.skip(1); // loop position
      }

      // Arpeggio macro
      {
        uint8_t env_size = r.u8();
        r.skip(static_cast<size_t>(env_size) * 4);
        if (env_size > 0)
          r.skip(1); // loop position
        r.skip(1);   // arpeggio macro mode
      }

      // Duty/Noise macro
      {
        uint8_t env_size = r.u8();
        r.skip(static_cast<size_t>(env_size) * 4);
        if (env_size > 0)
          r.skip(1); // loop position
      }

      // Wavetable macro
      {
        uint8_t env_size = r.u8();
        r.skip(static_cast<size_t>(env_size) * 4);
        if (env_size > 0)
          r.skip(1); // loop position
      }

      // Per-system data
      if (is_c64(system))
        r.skip(20);
      if (is_gameboy(system))
        r.skip(4);
    }
  }

  if (patches.empty()) {
    warnings.push_back("No FM instruments found in DMF file");
  }

  return ParseOk{std::move(patches), std::move(warnings)};
}

} // namespace ym2612_format::dmf
