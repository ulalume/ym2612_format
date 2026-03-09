#include "ym2612_format/ym2612_format.hpp"
#include "ym2612_format/detune.hpp"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ym2612_format;

// ---- Helpers ----

static int test_count = 0;
static int pass_count = 0;

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    auto _a = (a);                                                             \
    auto _b = (b);                                                             \
    if (_a != _b) {                                                            \
      std::cerr << "  FAIL: " << #a << " == " << #b << "\n"                   \
                << "    got: " << static_cast<int>(_a) << " vs "               \
                << static_cast<int>(_b) << "\n"                                \
                << "    at " << __FILE__ << ":" << __LINE__ << "\n";           \
      return false;                                                            \
    }                                                                          \
  } while (0)

#define ASSERT_TRUE(cond)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "  FAIL: " << #cond << "\n"                                \
                << "    at " << __FILE__ << ":" << __LINE__ << "\n";           \
      return false;                                                            \
    }                                                                          \
  } while (0)

#define RUN_TEST(func)                                                         \
  do {                                                                         \
    ++test_count;                                                              \
    std::cout << "  " << #func << " ... ";                                     \
    if (func()) {                                                              \
      ++pass_count;                                                            \
      std::cout << "ok\n";                                                     \
    } else {                                                                   \
      std::cout << "FAILED\n";                                                 \
    }                                                                          \
  } while (0)

static std::vector<uint8_t> read_file(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// Compare two patches, allowing hardware DT 0 and 4 to be considered equal
/// (both mean "no detune" on the YM2612).
static bool patches_equal(const Patch &a, const Patch &b) {
  if (a.name != b.name || a.algorithm != b.algorithm ||
      a.feedback != b.feedback || a.ams != b.ams || a.fms != b.fms ||
      a.dac_enable != b.dac_enable || a.lfo_enable != b.lfo_enable ||
      a.lfo_frequency != b.lfo_frequency || a.left != b.left ||
      a.right != b.right)
    return false;

  for (int i = 0; i < 4; ++i) {
    const auto &oa = a.operators[i];
    const auto &ob = b.operators[i];
    if (oa.ar != ob.ar || oa.dr != ob.dr || oa.sr != ob.sr ||
        oa.rr != ob.rr || oa.sl != ob.sl || oa.tl != ob.tl ||
        oa.ks != ob.ks || oa.ml != ob.ml ||
        oa.ssg_enable != ob.ssg_enable || oa.am != ob.am)
      return false;
    // When SSG-EG is disabled, the ssg value is meaningless on hardware;
    // some formats preserve it, others zero it out.
    if (oa.ssg_enable && oa.ssg != ob.ssg)
      return false;

    // DT: hardware 0 and 4 both mean "no detune"
    uint8_t dt_a = (oa.dt == 0) ? 4 : oa.dt;
    uint8_t dt_b = (ob.dt == 0) ? 4 : ob.dt;
    if (dt_a != dt_b)
      return false;
  }
  return true;
}

/// Compare two patches strictly (byte-exact).
static bool patches_strict_equal(const Patch &a, const Patch &b) {
  return a == b;
}

static void print_operator_diff(const Operator &a, const Operator &b, int idx) {
  if (a.ar != b.ar) std::cerr << "    OP" << idx << " AR: " << (int)a.ar << " vs " << (int)b.ar << "\n";
  if (a.dr != b.dr) std::cerr << "    OP" << idx << " DR: " << (int)a.dr << " vs " << (int)b.dr << "\n";
  if (a.sr != b.sr) std::cerr << "    OP" << idx << " SR: " << (int)a.sr << " vs " << (int)b.sr << "\n";
  if (a.rr != b.rr) std::cerr << "    OP" << idx << " RR: " << (int)a.rr << " vs " << (int)b.rr << "\n";
  if (a.sl != b.sl) std::cerr << "    OP" << idx << " SL: " << (int)a.sl << " vs " << (int)b.sl << "\n";
  if (a.tl != b.tl) std::cerr << "    OP" << idx << " TL: " << (int)a.tl << " vs " << (int)b.tl << "\n";
  if (a.ks != b.ks) std::cerr << "    OP" << idx << " KS: " << (int)a.ks << " vs " << (int)b.ks << "\n";
  if (a.ml != b.ml) std::cerr << "    OP" << idx << " ML: " << (int)a.ml << " vs " << (int)b.ml << "\n";
  if (a.dt != b.dt) std::cerr << "    OP" << idx << " DT: " << (int)a.dt << " vs " << (int)b.dt << "\n";
  if (a.ssg != b.ssg) std::cerr << "    OP" << idx << " SSG: " << (int)a.ssg << " vs " << (int)b.ssg << "\n";
  if (a.ssg_enable != b.ssg_enable) std::cerr << "    OP" << idx << " SSG_EN: " << a.ssg_enable << " vs " << b.ssg_enable << "\n";
  if (a.am != b.am) std::cerr << "    OP" << idx << " AM: " << a.am << " vs " << b.am << "\n";
}

static void print_patch_diff(const Patch &a, const Patch &b) {
  if (a.algorithm != b.algorithm) std::cerr << "    ALG: " << (int)a.algorithm << " vs " << (int)b.algorithm << "\n";
  if (a.feedback != b.feedback) std::cerr << "    FB: " << (int)a.feedback << " vs " << (int)b.feedback << "\n";
  if (a.ams != b.ams) std::cerr << "    AMS: " << (int)a.ams << " vs " << (int)b.ams << "\n";
  if (a.fms != b.fms) std::cerr << "    FMS: " << (int)a.fms << " vs " << (int)b.fms << "\n";
  for (int i = 0; i < 4; ++i)
    print_operator_diff(a.operators[i], b.operators[i], i + 1);
}

// ---- Detune conversion tests ----

bool test_detune_linear_to_hardware() {
  // Linear encoding: 0=-3, 1=-2, 2=-1, 3=0, 4=+1, 5=+2, 6=+3
  // Hardware encoding: 0=0, 1=+1, 2=+2, 3=+3, 4=0, 5=-1, 6=-2, 7=-3
  ASSERT_EQ(detune_from_linear(0), 7); // -3
  ASSERT_EQ(detune_from_linear(1), 6); // -2
  ASSERT_EQ(detune_from_linear(2), 5); // -1
  ASSERT_EQ(detune_from_linear(3), 4); // 0
  ASSERT_EQ(detune_from_linear(4), 1); // +1
  ASSERT_EQ(detune_from_linear(5), 2); // +2
  ASSERT_EQ(detune_from_linear(6), 3); // +3
  return true;
}

bool test_detune_hardware_to_linear() {
  ASSERT_EQ(detune_to_linear(0), 3); // 0
  ASSERT_EQ(detune_to_linear(1), 4); // +1
  ASSERT_EQ(detune_to_linear(2), 5); // +2
  ASSERT_EQ(detune_to_linear(3), 6); // +3
  ASSERT_EQ(detune_to_linear(4), 3); // 0
  ASSERT_EQ(detune_to_linear(5), 2); // -1
  ASSERT_EQ(detune_to_linear(6), 1); // -2
  ASSERT_EQ(detune_to_linear(7), 0); // -3
  return true;
}

bool test_detune_roundtrip_linear() {
  // to_linear(from_linear(x)) should be identity for valid linear values 0-6
  for (int i = 0; i <= 6; ++i) {
    uint8_t hw = detune_from_linear(i);
    uint8_t back = detune_to_linear(hw);
    ASSERT_EQ(back, static_cast<uint8_t>(i));
  }
  return true;
}

bool test_detune_roundtrip_hardware() {
  // from_linear(to_linear(x)) should preserve meaning for hardware values 0-7
  // Note: hardware 0 and 4 both mean "no detune" and may normalize to 4
  for (int i = 0; i <= 7; ++i) {
    uint8_t lin = detune_to_linear(i);
    uint8_t back = detune_from_linear(lin);
    // Hardware 0 → linear 3 → hardware 4 (both mean "no detune")
    if (i == 0) {
      ASSERT_EQ(back, 4);
    } else {
      ASSERT_EQ(back, static_cast<uint8_t>(i));
    }
  }
  return true;
}

// ---- DMP parse tests ----

/// "bright piano.dmp" — known values from raw hex analysis.
/// DMP raw DT bytes: 4, 2, 5, 3 (linear encoding)
/// Expected hardware DT: 1(+1), 5(-1), 2(+2), 4(0)
bool test_dmp_parse_bright_piano() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "bright piano.dmp");
  ASSERT_TRUE(!bytes.empty());

  auto result = dmp::parse(bytes.data(), bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(result));

  const auto &patches = get_ok(result).patches;
  ASSERT_EQ(patches.size(), 1u);

  const auto &p = patches[0];
  ASSERT_EQ(p.algorithm, 0);
  ASSERT_EQ(p.feedback, 5);
  ASSERT_EQ(p.ams, 0);
  ASSERT_EQ(p.fms, 0);

  // OP1: ML=7, TL=32, AR=25, DR=8, SL=5, RR=3, RS=0, DT_linear=4(→hw1), SR=6
  ASSERT_EQ(p.operators[0].ml, 7);
  ASSERT_EQ(p.operators[0].tl, 32);
  ASSERT_EQ(p.operators[0].ar, 25);
  ASSERT_EQ(p.operators[0].dr, 8);
  ASSERT_EQ(p.operators[0].sl, 5);
  ASSERT_EQ(p.operators[0].rr, 3);
  ASSERT_EQ(p.operators[0].ks, 0);
  ASSERT_EQ(p.operators[0].dt, 1); // linear 4 → hardware +1
  ASSERT_EQ(p.operators[0].sr, 6);

  // OP2: DT_linear=2 → hw5 (-1)
  ASSERT_EQ(p.operators[1].dt, 5);
  ASSERT_EQ(p.operators[1].ml, 3);
  ASSERT_EQ(p.operators[1].tl, 34);

  // OP3: DT_linear=5 → hw2 (+2)
  ASSERT_EQ(p.operators[2].dt, 2);
  ASSERT_EQ(p.operators[2].ml, 5);

  // OP4: DT_linear=3 → hw4 (0)
  ASSERT_EQ(p.operators[3].dt, 4);
  ASSERT_EQ(p.operators[3].ml, 1);

  return true;
}

bool test_dmp_parse_organ() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "organ.dmp");
  ASSERT_TRUE(!bytes.empty());

  auto result = dmp::parse(bytes.data(), bytes.size(), "organ");
  ASSERT_TRUE(is_ok(result));

  const auto &p = get_ok(result).patches[0];
  ASSERT_EQ(p.algorithm, 7);
  ASSERT_EQ(p.feedback, 4);

  // OP1 DT_linear=6 → hw3 (+3)
  ASSERT_EQ(p.operators[0].dt, 3);
  // OP2 DT_linear=4 → hw1 (+1)
  ASSERT_EQ(p.operators[1].dt, 1);
  // OP3 DT_linear=5 → hw2 (+2)
  ASSERT_EQ(p.operators[2].dt, 2);
  // OP4 DT_linear=3 → hw4 (0)
  ASSERT_EQ(p.operators[3].dt, 4);

  return true;
}

bool test_dmp_parse_acoustic_bass() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "acoustic bass.dmp");
  ASSERT_TRUE(!bytes.empty());

  auto result = dmp::parse(bytes.data(), bytes.size(), "acoustic bass");
  ASSERT_TRUE(is_ok(result));

  const auto &p = get_ok(result).patches[0];
  ASSERT_EQ(p.algorithm, 1);
  ASSERT_EQ(p.feedback, 2);

  // Raw DT bytes: 2, 0, 1, 3 (linear encoding)
  ASSERT_EQ(p.operators[0].dt, 5); // linear 2 → hw -1
  ASSERT_EQ(p.operators[1].dt, 7); // linear 0 → hw -3
  ASSERT_EQ(p.operators[2].dt, 6); // linear 1 → hw -2
  ASSERT_EQ(p.operators[3].dt, 4); // linear 3 → hw 0

  return true;
}

// ---- DMP roundtrip tests ----

bool test_dmp_roundtrip() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "bright piano.dmp");
  ASSERT_TRUE(!bytes.empty());

  auto result = dmp::parse(bytes.data(), bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(result));
  const auto &original = get_ok(result).patches[0];

  // Serialize back to DMP
  auto ser = dmp::serialize(original);
  ASSERT_TRUE(is_ok(ser));

  // Re-parse
  const auto &dmp_bytes = get_ok(ser);
  auto result2 = dmp::parse(dmp_bytes.data(), dmp_bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(result2));
  const auto &roundtripped = get_ok(result2).patches[0];

  ASSERT_TRUE(patches_equal(original, roundtripped));
  return true;
}

// ---- Cross-format roundtrip tests ----

bool test_dmp_to_fui_roundtrip() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "bright piano.dmp");
  ASSERT_TRUE(!bytes.empty());

  auto result = dmp::parse(bytes.data(), bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(result));
  const auto &original = get_ok(result).patches[0];

  // DMP → FUI
  auto fui_bytes_r = fui::serialize(original);
  ASSERT_TRUE(is_ok(fui_bytes_r));
  const auto &fui_bytes = get_ok(fui_bytes_r);

  // FUI → Patch
  auto fui_result = fui::parse(fui_bytes.data(), fui_bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(fui_result));
  const auto &roundtripped = get_ok(fui_result).patches[0];

  if (!patches_equal(original, roundtripped)) {
    std::cerr << "  DMP→FUI roundtrip diff:\n";
    print_patch_diff(original, roundtripped);
    return false;
  }
  return true;
}

bool test_dmp_to_gin_roundtrip() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "bright piano.dmp");
  ASSERT_TRUE(!bytes.empty());

  auto result = dmp::parse(bytes.data(), bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(result));
  const auto &original = get_ok(result).patches[0];

  // DMP → GIN
  auto gin_bytes_r = gin::serialize(original);
  ASSERT_TRUE(is_ok(gin_bytes_r));
  const auto &gin_bytes = get_ok(gin_bytes_r);

  // GIN → Patch
  auto gin_result = gin::parse(gin_bytes.data(), gin_bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(gin_result));
  const auto &roundtripped = get_ok(gin_result).patches[0];

  // GIN stores hardware encoding directly, so strict equality should hold
  ASSERT_TRUE(patches_strict_equal(original, roundtripped));
  return true;
}

bool test_dmp_to_mml_roundtrip() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "bright piano.dmp");
  ASSERT_TRUE(!bytes.empty());

  auto result = dmp::parse(bytes.data(), bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(result));
  const auto &original = get_ok(result).patches[0];

  // DMP → MML (text)
  auto mml_result = ctrmml::serialize_text(original);
  ASSERT_TRUE(is_ok(mml_result));
  const auto &mml_text = get_ok(mml_result);

  // MML → Patch
  auto mml_bytes = std::vector<uint8_t>(mml_text.begin(), mml_text.end());
  auto mml_parsed = ctrmml::parse(mml_bytes.data(), mml_bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(mml_parsed));
  const auto &roundtripped = get_ok(mml_parsed).patches[0];

  // MML stores hardware DT directly and the name may differ
  // Check all operator parameters except name
  for (int i = 0; i < 4; ++i) {
    const auto &oa = original.operators[i];
    const auto &ob = roundtripped.operators[i];
    ASSERT_EQ(oa.ar, ob.ar);
    ASSERT_EQ(oa.dr, ob.dr);
    ASSERT_EQ(oa.sr, ob.sr);
    ASSERT_EQ(oa.rr, ob.rr);
    ASSERT_EQ(oa.sl, ob.sl);
    ASSERT_EQ(oa.tl, ob.tl);
    ASSERT_EQ(oa.ks, ob.ks);
    ASSERT_EQ(oa.ml, ob.ml);
    ASSERT_EQ(oa.dt, ob.dt);
    ASSERT_EQ(oa.ssg, ob.ssg);
    ASSERT_EQ(oa.ssg_enable, ob.ssg_enable);
    ASSERT_EQ(oa.am, ob.am);
  }
  ASSERT_EQ(original.algorithm, roundtripped.algorithm);
  ASSERT_EQ(original.feedback, roundtripped.feedback);
  return true;
}

// ---- DMF tests ----

bool test_dmf_parse_detune() {
  auto bytes = read_file(
      fs::path(SAMPLE_DATA_DIR) / "OPN2_Instruments" / "[OPN2_Instruments].dmf");
  if (bytes.empty()) {
    std::cerr << "  SKIP: DMF test file not found\n";
    return true; // skip gracefully
  }

  auto result = dmf::parse(bytes.data(), bytes.size(), "opn2");
  ASSERT_TRUE(is_ok(result));

  const auto &patches = get_ok(result).patches;
  ASSERT_TRUE(patches.size() >= 10);

  // Find Bass_l1 — known raw DT bytes: 3, 6, 6, 3 (linear encoding)
  // Expected hardware DT: 4(0), 3(+3), 3(+3), 4(0)
  const Patch *bass_l1 = nullptr;
  const Patch *bass_l2 = nullptr;
  const Patch *bell1 = nullptr;
  const Patch *brass1 = nullptr;

  for (const auto &p : patches) {
    if (p.name == "Bass_l1") bass_l1 = &p;
    if (p.name == "Bass_l2") bass_l2 = &p;
    if (p.name == "Bell1") bell1 = &p;
    if (p.name == "Brass1") brass1 = &p;
  }

  // Bass_l1: raw DT [3, 6, 6, 3] → hardware [4, 3, 3, 4]
  ASSERT_TRUE(bass_l1 != nullptr);
  ASSERT_EQ(bass_l1->operators[0].dt, 4); // linear 3 → 0
  ASSERT_EQ(bass_l1->operators[1].dt, 3); // linear 6 → +3
  ASSERT_EQ(bass_l1->operators[2].dt, 3); // linear 6 → +3
  ASSERT_EQ(bass_l1->operators[3].dt, 4); // linear 3 → 0

  // Bass_l2: raw DT [3, 3, 3, 3] → all hardware 4 (no detune)
  ASSERT_TRUE(bass_l2 != nullptr);
  for (int i = 0; i < 4; ++i)
    ASSERT_EQ(bass_l2->operators[i].dt, 4);

  // Bell1: raw DT [3, 0, 6, 6] → hardware [4, 7, 3, 3]
  ASSERT_TRUE(bell1 != nullptr);
  ASSERT_EQ(bell1->operators[0].dt, 4); // linear 3 → 0
  ASSERT_EQ(bell1->operators[1].dt, 7); // linear 0 → -3
  ASSERT_EQ(bell1->operators[2].dt, 3); // linear 6 → +3
  ASSERT_EQ(bell1->operators[3].dt, 3); // linear 6 → +3

  // Brass1: raw DT [3, 1, 3, 5] → hardware [4, 6, 4, 2]
  ASSERT_TRUE(brass1 != nullptr);
  ASSERT_EQ(brass1->operators[0].dt, 4); // linear 3 → 0
  ASSERT_EQ(brass1->operators[1].dt, 6); // linear 1 → -2
  ASSERT_EQ(brass1->operators[2].dt, 4); // linear 3 → 0
  ASSERT_EQ(brass1->operators[3].dt, 2); // linear 5 → +2

  return true;
}

bool test_dmf_to_fui_roundtrip() {
  auto bytes = read_file(
      fs::path(SAMPLE_DATA_DIR) / "OPN2_Instruments" / "[OPN2_Instruments].dmf");
  if (bytes.empty()) {
    std::cerr << "  SKIP: DMF test file not found\n";
    return true;
  }

  auto result = dmf::parse(bytes.data(), bytes.size(), "opn2");
  ASSERT_TRUE(is_ok(result));

  const auto &patches = get_ok(result).patches;
  int tested = 0;

  for (const auto &original : patches) {
    // DMF → FUI
    auto fui_bytes_r = fui::serialize(original);
    ASSERT_TRUE(is_ok(fui_bytes_r));
    const auto &fui_bytes = get_ok(fui_bytes_r);

    // FUI → Patch
    auto fui_result = fui::parse(fui_bytes.data(), fui_bytes.size(), original.name);
    ASSERT_TRUE(is_ok(fui_result));
    const auto &roundtripped = get_ok(fui_result).patches[0];

    if (!patches_equal(original, roundtripped)) {
      std::cerr << "  DMF→FUI roundtrip failed for '" << original.name << "':\n";
      print_patch_diff(original, roundtripped);
      return false;
    }
    ++tested;
  }

  ASSERT_TRUE(tested >= 10);
  return true;
}

bool test_dmf_to_dmp_roundtrip() {
  auto bytes = read_file(
      fs::path(SAMPLE_DATA_DIR) / "OPN2_Instruments" / "[OPN2_Instruments].dmf");
  if (bytes.empty()) {
    std::cerr << "  SKIP: DMF test file not found\n";
    return true;
  }

  auto result = dmf::parse(bytes.data(), bytes.size(), "opn2");
  ASSERT_TRUE(is_ok(result));

  for (const auto &original : get_ok(result).patches) {
    // DMF → DMP
    auto dmp_bytes_r = dmp::serialize(original);
    ASSERT_TRUE(is_ok(dmp_bytes_r));
    const auto &dmp_bytes = get_ok(dmp_bytes_r);

    // DMP → Patch
    auto dmp_result = dmp::parse(dmp_bytes.data(), dmp_bytes.size(), original.name);
    ASSERT_TRUE(is_ok(dmp_result));
    const auto &roundtripped = get_ok(dmp_result).patches[0];

    if (!patches_equal(original, roundtripped)) {
      std::cerr << "  DMF→DMP roundtrip failed for '" << original.name << "':\n";
      print_patch_diff(original, roundtripped);
      return false;
    }
  }
  return true;
}

// ---- Exhaustive detune value test ----

/// Test every detune value (0-7) through all writable formats.
bool test_all_detune_values_roundtrip() {
  for (int dt_val = 0; dt_val <= 7; ++dt_val) {
    Patch patch;
    patch.name = "dt_test_" + std::to_string(dt_val);
    patch.algorithm = 2;
    patch.feedback = 3;
    // Set all operators to distinct values with the test DT
    for (int op = 0; op < 4; ++op) {
      auto &o = patch.operators[op];
      o.ar = 20 + op;
      o.dr = 10 + op;
      o.sr = 5 + op;
      o.rr = 8 + op;
      o.sl = 7;
      o.tl = 30 + op * 10;
      o.ks = op % 4;
      o.ml = 1 + op;
      o.dt = static_cast<uint8_t>(dt_val);
    }

    // Hardware 0 and 4 both mean "no detune".
    // After roundtrip through linear-based formats (DMP, FUI), hw 0 becomes hw 4.
    uint8_t expected_dt = (dt_val == 0) ? 4 : static_cast<uint8_t>(dt_val);

    // Test DMP roundtrip
    {
      auto ser = dmp::serialize(patch);
      ASSERT_TRUE(is_ok(ser));
      auto parsed = dmp::parse(get_ok(ser).data(), get_ok(ser).size(), patch.name);
      ASSERT_TRUE(is_ok(parsed));
      for (int op = 0; op < 4; ++op) {
        if (get_ok(parsed).patches[0].operators[op].dt != expected_dt) {
          std::cerr << "  DMP roundtrip failed for DT=" << dt_val
                    << " OP" << op << ": got "
                    << (int)get_ok(parsed).patches[0].operators[op].dt
                    << " expected " << (int)expected_dt << "\n";
          return false;
        }
      }
    }

    // Test FUI roundtrip
    {
      auto ser = fui::serialize(patch);
      ASSERT_TRUE(is_ok(ser));
      auto parsed = fui::parse(get_ok(ser).data(), get_ok(ser).size(), patch.name);
      ASSERT_TRUE(is_ok(parsed));
      for (int op = 0; op < 4; ++op) {
        if (get_ok(parsed).patches[0].operators[op].dt != expected_dt) {
          std::cerr << "  FUI roundtrip failed for DT=" << dt_val
                    << " OP" << op << ": got "
                    << (int)get_ok(parsed).patches[0].operators[op].dt
                    << " expected " << (int)expected_dt << "\n";
          return false;
        }
      }
    }

    // Test GIN roundtrip (stores hardware directly, preserves 0 vs 4)
    {
      auto ser = gin::serialize(patch);
      ASSERT_TRUE(is_ok(ser));
      auto parsed = gin::parse(get_ok(ser).data(), get_ok(ser).size(), patch.name);
      ASSERT_TRUE(is_ok(parsed));
      for (int op = 0; op < 4; ++op) {
        ASSERT_EQ(get_ok(parsed).patches[0].operators[op].dt,
                  static_cast<uint8_t>(dt_val));
      }
    }

    // Test MML roundtrip (stores hardware directly)
    {
      auto ser = ctrmml::serialize_text(patch);
      ASSERT_TRUE(is_ok(ser));
      auto text = get_ok(ser);
      auto text_bytes = std::vector<uint8_t>(text.begin(), text.end());
      auto parsed = ctrmml::parse(text_bytes.data(), text_bytes.size(), patch.name);
      ASSERT_TRUE(is_ok(parsed));
      for (int op = 0; op < 4; ++op) {
        ASSERT_EQ(get_ok(parsed).patches[0].operators[op].dt,
                  static_cast<uint8_t>(dt_val));
      }
    }
  }
  return true;
}

// ---- All-parameter roundtrip test ----

/// Create a patch that exercises all parameter ranges, then roundtrip through
/// every writable format.
bool test_full_parameter_roundtrip() {
  Patch patch;
  patch.name = "full_test";
  patch.algorithm = 5;
  patch.feedback = 6;
  patch.ams = 2;
  patch.fms = 5;
  patch.lfo_enable = false;
  patch.lfo_frequency = 0;
  patch.dac_enable = false;
  patch.left = true;
  patch.right = true;

  // OP1: moderate values
  patch.operators[0] = {31, 20, 15, 12, 10, 80, 2, 8, 1, 0, false, false, true};
  // OP2: max values in some fields
  patch.operators[1] = {31, 31, 31, 15, 15, 127, 3, 15, 7, 5, true, true, true};
  // OP3: min values
  patch.operators[2] = {0, 0, 0, 0, 0, 0, 0, 0, 4, 0, false, false, true};
  // OP4: SSG enabled
  patch.operators[3] = {28, 14, 8, 10, 7, 45, 1, 5, 3, 3, true, false, true};

  struct {
    const char *name;
    SerializeResult (*serialize)(const Patch &);
    ParseResult (*parse)(const uint8_t *, size_t, const std::string &);
    bool is_linear_dt; // uses linear DT encoding (0↔4 normalization)
  } formats[] = {
      {"DMP", dmp::serialize, dmp::parse, true},
      {"FUI", fui::serialize, fui::parse, true},
      {"GIN", gin::serialize, gin::parse, false},
  };

  for (const auto &fmt : formats) {
    auto ser = fmt.serialize(patch);
    if (!is_ok(ser)) {
      std::cerr << "  " << fmt.name << " serialize failed: "
                << get_error(ser).message << "\n";
      return false;
    }

    auto parsed = fmt.parse(get_ok(ser).data(), get_ok(ser).size(), patch.name);
    if (!is_ok(parsed)) {
      std::cerr << "  " << fmt.name << " parse failed: "
                << get_error(parsed).message << "\n";
      return false;
    }

    const auto &rp = get_ok(parsed).patches[0];

    if (fmt.is_linear_dt) {
      if (!patches_equal(patch, rp)) {
        std::cerr << "  " << fmt.name << " roundtrip diff:\n";
        print_patch_diff(patch, rp);
        return false;
      }
    } else {
      if (!patches_strict_equal(patch, rp)) {
        std::cerr << "  " << fmt.name << " roundtrip diff:\n";
        print_patch_diff(patch, rp);
        return false;
      }
    }
  }

  // MML roundtrip (lossy for name, ams/fms stored as comment)
  {
    auto ser = ctrmml::serialize_text(patch);
    ASSERT_TRUE(is_ok(ser));
    auto text = get_ok(ser);
    auto text_bytes = std::vector<uint8_t>(text.begin(), text.end());
    auto parsed = ctrmml::parse(text_bytes.data(), text_bytes.size(), patch.name);
    ASSERT_TRUE(is_ok(parsed));
    const auto &rp = get_ok(parsed).patches[0];

    // MML preserves: algorithm, feedback, operators
    // MML loses: ams, fms, lfo, panning (stored as comments)
    ASSERT_EQ(patch.algorithm, rp.algorithm);
    ASSERT_EQ(patch.feedback, rp.feedback);
    for (int i = 0; i < 4; ++i) {
      ASSERT_EQ(patch.operators[i].ar, rp.operators[i].ar);
      ASSERT_EQ(patch.operators[i].dr, rp.operators[i].dr);
      ASSERT_EQ(patch.operators[i].sr, rp.operators[i].sr);
      ASSERT_EQ(patch.operators[i].rr, rp.operators[i].rr);
      ASSERT_EQ(patch.operators[i].sl, rp.operators[i].sl);
      ASSERT_EQ(patch.operators[i].tl, rp.operators[i].tl);
      ASSERT_EQ(patch.operators[i].ks, rp.operators[i].ks);
      ASSERT_EQ(patch.operators[i].ml, rp.operators[i].ml);
      ASSERT_EQ(patch.operators[i].dt, rp.operators[i].dt);
      ASSERT_EQ(patch.operators[i].ssg, rp.operators[i].ssg);
      ASSERT_EQ(patch.operators[i].ssg_enable, rp.operators[i].ssg_enable);
      ASSERT_EQ(patch.operators[i].am, rp.operators[i].am);
    }
  }

  return true;
}

// ---- FUI macro roundtrip tests ----

/// Helper to compare macros
static bool macros_equal(const Macro &a, const Macro &b) {
  if (a.empty() != b.empty()) return false;
  if (a.empty()) return true;
  return a.values == b.values && a.loop == b.loop && a.release == b.release &&
         a.type == b.type && a.speed == b.speed && a.delay == b.delay;
}

/// Create a patch with channel macros and verify FUI roundtrip.
bool test_fui_channel_macro_roundtrip() {
  Patch patch;
  patch.name = "macro_test";
  patch.algorithm = 2;
  patch.feedback = 3;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 1; op.ml = 3;
    op.dt = 4;
  }

  // Volume macro: u8 values
  patch.macros.volume.values = {15, 14, 13, 12, 10, 8, 6, 4};
  patch.macros.volume.loop = 2;
  patch.macros.volume.release = 6;
  patch.macros.volume.speed = 1;
  patch.macros.volume.delay = 0;
  patch.macros.volume.type = MacroType::Sequence;

  // Arpeggio macro: i8 values (negative)
  patch.macros.arpeggio.values = {0, 12, -12, 7, -5};
  patch.macros.arpeggio.loop = 255;
  patch.macros.arpeggio.release = 255;
  patch.macros.arpeggio.speed = 2;
  patch.macros.arpeggio.delay = 1;
  patch.macros.arpeggio.type = MacroType::Sequence;

  // Pitch macro: i16 values (large range)
  patch.macros.pitch.values = {-300, 200, -150, 500, 0};
  patch.macros.pitch.loop = 0;
  patch.macros.pitch.release = 3;
  patch.macros.pitch.speed = 1;
  patch.macros.pitch.delay = 0;
  patch.macros.pitch.type = MacroType::Sequence;

  // Algorithm macro: single value
  patch.macros.algorithm.values = {2};
  patch.macros.algorithm.loop = 255;
  patch.macros.algorithm.release = 255;
  patch.macros.algorithm.speed = 1;
  patch.macros.algorithm.delay = 0;
  patch.macros.algorithm.type = MacroType::Sequence;

  // Serialize to FUI
  auto ser = fui::serialize(patch);
  ASSERT_TRUE(is_ok(ser));
  const auto &fui_bytes = get_ok(ser);

  // Parse back
  auto parsed = fui::parse(fui_bytes.data(), fui_bytes.size(), "macro_test");
  ASSERT_TRUE(is_ok(parsed));
  const auto &rp = get_ok(parsed).patches[0];

  // Check static params first
  ASSERT_EQ(rp.algorithm, 2);
  ASSERT_EQ(rp.feedback, 3);

  // Check macros
  ASSERT_TRUE(macros_equal(patch.macros.volume, rp.macros.volume));
  ASSERT_TRUE(macros_equal(patch.macros.arpeggio, rp.macros.arpeggio));
  ASSERT_TRUE(macros_equal(patch.macros.pitch, rp.macros.pitch));
  ASSERT_TRUE(macros_equal(patch.macros.algorithm, rp.macros.algorithm));

  // Unused macros should be empty
  ASSERT_TRUE(rp.macros.duty.empty());
  ASSERT_TRUE(rp.macros.wave.empty());
  ASSERT_TRUE(rp.macros.feedback.empty());
  ASSERT_TRUE(rp.macros.fms.empty());
  ASSERT_TRUE(rp.macros.ams.empty());
  ASSERT_TRUE(rp.macros.pan_left.empty());
  ASSERT_TRUE(rp.macros.pan_right.empty());
  ASSERT_TRUE(rp.macros.phase_reset.empty());
  ASSERT_TRUE(rp.macros.ex1.empty());
  ASSERT_TRUE(rp.macros.ex2.empty());
  ASSERT_TRUE(rp.macros.ex3.empty());

  return true;
}

/// Create a patch with operator macros and verify FUI roundtrip.
bool test_fui_operator_macro_roundtrip() {
  Patch patch;
  patch.name = "op_macro_test";
  patch.algorithm = 4;
  patch.feedback = 2;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 25; op.dr = 12; op.sr = 8; op.rr = 6;
    op.sl = 5; op.tl = 40; op.ks = 0; op.ml = 1;
    op.dt = 4;
  }

  // OP1 TL macro
  patch.operator_macros[0].tl.values = {127, 120, 100, 80, 60, 40};
  patch.operator_macros[0].tl.loop = 3;
  patch.operator_macros[0].tl.release = 5;
  patch.operator_macros[0].tl.speed = 1;

  // OP1 AR macro
  patch.operator_macros[0].ar.values = {31, 28, 24, 20};
  patch.operator_macros[0].ar.loop = 255;
  patch.operator_macros[0].ar.release = 255;
  patch.operator_macros[0].ar.speed = 1;

  // OP2 DT macro with negative values (i8 range)
  patch.operator_macros[1].dt.values = {-3, -1, 0, 1, 3};
  patch.operator_macros[1].dt.loop = 0;
  patch.operator_macros[1].dt.release = 255;
  patch.operator_macros[1].dt.speed = 2;
  patch.operator_macros[1].dt.delay = 3;

  // OP3 ML macro
  patch.operator_macros[2].ml.values = {1, 2, 3, 4, 5, 6, 7, 8};
  patch.operator_macros[2].ml.loop = 0;
  patch.operator_macros[2].ml.release = 255;
  patch.operator_macros[2].ml.speed = 1;

  // OP4 SSG macro
  patch.operator_macros[3].ssg.values = {0, 4, 5, 6, 7};
  patch.operator_macros[3].ssg.loop = 255;
  patch.operator_macros[3].ssg.release = 255;
  patch.operator_macros[3].ssg.speed = 1;

  // Serialize to FUI
  auto ser = fui::serialize(patch);
  ASSERT_TRUE(is_ok(ser));
  const auto &fui_bytes = get_ok(ser);

  // Parse back
  auto parsed = fui::parse(fui_bytes.data(), fui_bytes.size(), "op_macro_test");
  ASSERT_TRUE(is_ok(parsed));
  const auto &rp = get_ok(parsed).patches[0];

  // Check OP1 macros
  ASSERT_TRUE(macros_equal(patch.operator_macros[0].tl, rp.operator_macros[0].tl));
  ASSERT_TRUE(macros_equal(patch.operator_macros[0].ar, rp.operator_macros[0].ar));
  ASSERT_TRUE(rp.operator_macros[0].dr.empty());

  // Check OP2 macros
  ASSERT_TRUE(macros_equal(patch.operator_macros[1].dt, rp.operator_macros[1].dt));
  ASSERT_TRUE(rp.operator_macros[1].tl.empty());

  // Check OP3 macros
  ASSERT_TRUE(macros_equal(patch.operator_macros[2].ml, rp.operator_macros[2].ml));

  // Check OP4 macros
  ASSERT_TRUE(macros_equal(patch.operator_macros[3].ssg, rp.operator_macros[3].ssg));

  return true;
}

/// Test i32 macro values (large range).
bool test_fui_macro_i32_values() {
  Patch patch;
  patch.name = "i32_macro_test";
  patch.algorithm = 0;
  patch.feedback = 0;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 0; op.ml = 1; op.dt = 4;
  }

  // Pitch macro with i32 range values
  patch.macros.pitch.values = {-50000, 50000, -100, 100, 0};
  patch.macros.pitch.loop = 255;
  patch.macros.pitch.release = 255;
  patch.macros.pitch.speed = 1;
  patch.macros.pitch.delay = 0;

  auto ser = fui::serialize(patch);
  ASSERT_TRUE(is_ok(ser));
  const auto &fui_bytes = get_ok(ser);

  auto parsed = fui::parse(fui_bytes.data(), fui_bytes.size(), "i32_macro_test");
  ASSERT_TRUE(is_ok(parsed));
  const auto &rp = get_ok(parsed).patches[0];

  ASSERT_TRUE(macros_equal(patch.macros.pitch, rp.macros.pitch));

  // Verify exact values
  ASSERT_EQ(rp.macros.pitch.values.size(), 5u);
  ASSERT_EQ(rp.macros.pitch.values[0], -50000);
  ASSERT_EQ(rp.macros.pitch.values[1], 50000);
  ASSERT_EQ(rp.macros.pitch.values[2], -100);
  ASSERT_EQ(rp.macros.pitch.values[3], 100);
  ASSERT_EQ(rp.macros.pitch.values[4], 0);

  return true;
}

/// Test that a patch without macros still roundtrips correctly.
bool test_fui_no_macro_roundtrip() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "bright piano.dmp");
  ASSERT_TRUE(!bytes.empty());

  auto dmp_result = dmp::parse(bytes.data(), bytes.size(), "bright piano");
  ASSERT_TRUE(is_ok(dmp_result));
  const auto &original = get_ok(dmp_result).patches[0];

  // Ensure no macros
  ASSERT_TRUE(!original.has_macros());

  // FUI roundtrip
  auto ser = fui::serialize(original);
  ASSERT_TRUE(is_ok(ser));
  auto parsed = fui::parse(get_ok(ser).data(), get_ok(ser).size(), "bright piano");
  ASSERT_TRUE(is_ok(parsed));
  const auto &rp = get_ok(parsed).patches[0];

  ASSERT_TRUE(!rp.has_macros());
  ASSERT_TRUE(patches_equal(original, rp));

  return true;
}

/// Test all 15 channel macro codes.
bool test_fui_all_channel_macro_codes() {
  Patch patch;
  patch.name = "all_ch_macros";
  patch.algorithm = 0;
  patch.feedback = 0;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 0; op.ml = 1; op.dt = 4;
  }

  // Set all 15 channel macros
  auto make_macro = [](std::vector<int32_t> vals, uint8_t loop = 255) {
    Macro m;
    m.values = std::move(vals);
    m.loop = loop;
    m.release = 255;
    m.speed = 1;
    m.delay = 0;
    m.type = MacroType::Sequence;
    return m;
  };

  patch.macros.volume      = make_macro({15, 14, 13}, 0);
  patch.macros.arpeggio    = make_macro({0, 12, 24});
  patch.macros.duty        = make_macro({1, 2});
  patch.macros.wave        = make_macro({0, 1, 2, 3});
  patch.macros.pitch       = make_macro({-100, 0, 100});
  patch.macros.ex1         = make_macro({1});
  patch.macros.ex2         = make_macro({2, 3});
  patch.macros.ex3         = make_macro({4, 5, 6});
  patch.macros.algorithm   = make_macro({0, 1, 2, 3, 4, 5, 6, 7}, 0);
  patch.macros.feedback    = make_macro({7, 6, 5, 4});
  patch.macros.fms         = make_macro({0, 1, 2});
  patch.macros.ams         = make_macro({0, 1, 2, 3});
  patch.macros.pan_left    = make_macro({0, 1, 1, 1});
  patch.macros.pan_right   = make_macro({1, 1, 0, 0});
  patch.macros.phase_reset = make_macro({0, 1, 0});

  auto ser = fui::serialize(patch);
  ASSERT_TRUE(is_ok(ser));

  auto parsed = fui::parse(get_ok(ser).data(), get_ok(ser).size(), "all_ch_macros");
  ASSERT_TRUE(is_ok(parsed));
  const auto &rp = get_ok(parsed).patches[0];

  ASSERT_TRUE(macros_equal(patch.macros.volume, rp.macros.volume));
  ASSERT_TRUE(macros_equal(patch.macros.arpeggio, rp.macros.arpeggio));
  ASSERT_TRUE(macros_equal(patch.macros.duty, rp.macros.duty));
  ASSERT_TRUE(macros_equal(patch.macros.wave, rp.macros.wave));
  ASSERT_TRUE(macros_equal(patch.macros.pitch, rp.macros.pitch));
  ASSERT_TRUE(macros_equal(patch.macros.ex1, rp.macros.ex1));
  ASSERT_TRUE(macros_equal(patch.macros.ex2, rp.macros.ex2));
  ASSERT_TRUE(macros_equal(patch.macros.ex3, rp.macros.ex3));
  ASSERT_TRUE(macros_equal(patch.macros.algorithm, rp.macros.algorithm));
  ASSERT_TRUE(macros_equal(patch.macros.feedback, rp.macros.feedback));
  ASSERT_TRUE(macros_equal(patch.macros.fms, rp.macros.fms));
  ASSERT_TRUE(macros_equal(patch.macros.ams, rp.macros.ams));
  ASSERT_TRUE(macros_equal(patch.macros.pan_left, rp.macros.pan_left));
  ASSERT_TRUE(macros_equal(patch.macros.pan_right, rp.macros.pan_right));
  ASSERT_TRUE(macros_equal(patch.macros.phase_reset, rp.macros.phase_reset));

  return true;
}

// ---- GIN macro roundtrip tests ----

/// GIN macro roundtrip with channel + operator macros.
bool test_gin_macro_roundtrip() {
  Patch patch;
  patch.name = "gin_macro_test";
  patch.algorithm = 3;
  patch.feedback = 5;
  patch.left = true;
  patch.right = true;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 1; op.ml = 3; op.dt = 4;
  }

  // Channel macros
  patch.macros.volume.values = {15, 14, 13, 12, 10};
  patch.macros.volume.loop = 2;
  patch.macros.volume.release = 4;
  patch.macros.volume.speed = 1;

  patch.macros.arpeggio.values = {0, 12, -12, 7};
  patch.macros.arpeggio.speed = 2;
  patch.macros.arpeggio.delay = 1;

  patch.macros.pitch.values = {-300, 200, 0};
  patch.macros.pitch.loop = 0;
  patch.macros.pitch.release = 2;

  patch.macros.algorithm.values = {3, 4, 5};
  patch.macros.feedback.values = {5, 4, 3, 2};
  patch.macros.fms.values = {0, 1, 2};
  patch.macros.ams.values = {0, 1};
  patch.macros.pan_left.values = {1, 1, 0};
  patch.macros.pan_right.values = {1, 0, 1};

  // Operator macros
  patch.operator_macros[0].tl.values = {127, 100, 80, 60};
  patch.operator_macros[0].tl.loop = 1;
  patch.operator_macros[1].ar.values = {31, 28, 24};
  patch.operator_macros[2].dt.values = {-3, 0, 3};
  patch.operator_macros[2].dt.speed = 2;
  patch.operator_macros[3].ssg.values = {0, 4, 5, 7};

  // Serialize to GIN
  auto ser = gin::serialize(patch);
  ASSERT_TRUE(is_ok(ser));
  const auto &gin_bytes = get_ok(ser);

  // Parse back
  auto parsed = gin::parse(gin_bytes.data(), gin_bytes.size(), "gin_macro_test");
  ASSERT_TRUE(is_ok(parsed));
  const auto &rp = get_ok(parsed).patches[0];

  // Static params
  ASSERT_TRUE(patches_strict_equal(patch, rp));

  // Channel macros
  ASSERT_TRUE(macros_equal(patch.macros.volume, rp.macros.volume));
  ASSERT_TRUE(macros_equal(patch.macros.arpeggio, rp.macros.arpeggio));
  ASSERT_TRUE(macros_equal(patch.macros.pitch, rp.macros.pitch));
  ASSERT_TRUE(macros_equal(patch.macros.algorithm, rp.macros.algorithm));
  ASSERT_TRUE(macros_equal(patch.macros.feedback, rp.macros.feedback));
  ASSERT_TRUE(macros_equal(patch.macros.fms, rp.macros.fms));
  ASSERT_TRUE(macros_equal(patch.macros.ams, rp.macros.ams));
  ASSERT_TRUE(macros_equal(patch.macros.pan_left, rp.macros.pan_left));
  ASSERT_TRUE(macros_equal(patch.macros.pan_right, rp.macros.pan_right));

  // Unused channel macros should be empty
  ASSERT_TRUE(rp.macros.duty.empty());
  ASSERT_TRUE(rp.macros.wave.empty());
  ASSERT_TRUE(rp.macros.phase_reset.empty());
  ASSERT_TRUE(rp.macros.ex1.empty());
  ASSERT_TRUE(rp.macros.ex2.empty());
  ASSERT_TRUE(rp.macros.ex3.empty());

  // Operator macros
  ASSERT_TRUE(macros_equal(patch.operator_macros[0].tl, rp.operator_macros[0].tl));
  ASSERT_TRUE(macros_equal(patch.operator_macros[1].ar, rp.operator_macros[1].ar));
  ASSERT_TRUE(macros_equal(patch.operator_macros[2].dt, rp.operator_macros[2].dt));
  ASSERT_TRUE(macros_equal(patch.operator_macros[3].ssg, rp.operator_macros[3].ssg));

  // Unused op macros should be empty
  ASSERT_TRUE(rp.operator_macros[0].ar.empty());
  ASSERT_TRUE(rp.operator_macros[1].tl.empty());

  return true;
}

/// GIN without macros should produce no "macros" key (backward compatible).
bool test_gin_no_macro_backward_compat() {
  Patch patch;
  patch.name = "no_macro";
  patch.algorithm = 0;
  patch.feedback = 0;
  patch.left = true;
  patch.right = true;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 0; op.ml = 1; op.dt = 4;
  }

  ASSERT_TRUE(!patch.has_macros());

  auto ser = gin::serialize(patch);
  ASSERT_TRUE(is_ok(ser));
  const auto &gin_bytes = get_ok(ser);

  // Verify no "macros" key in JSON output
  std::string json_text(gin_bytes.begin(), gin_bytes.end());
  ASSERT_TRUE(json_text.find("macros") == std::string::npos);

  // Parse back
  auto parsed = gin::parse(gin_bytes.data(), gin_bytes.size(), "no_macro");
  ASSERT_TRUE(is_ok(parsed));
  const auto &rp = get_ok(parsed).patches[0];

  ASSERT_TRUE(!rp.has_macros());
  ASSERT_TRUE(patches_strict_equal(patch, rp));

  return true;
}

/// FUI → GIN → FUI cross-format macro roundtrip.
bool test_fui_to_gin_macro_roundtrip() {
  Patch patch;
  patch.name = "cross_macro";
  patch.algorithm = 5;
  patch.feedback = 4;
  patch.left = true;
  patch.right = true;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 25; op.dr = 12; op.sr = 8; op.rr = 6;
    op.sl = 5; op.tl = 40; op.ks = 2; op.ml = 4; op.dt = 1;
  }

  // Channel macros
  patch.macros.volume.values = {15, 12, 8, 4, 0};
  patch.macros.volume.loop = 0;
  patch.macros.pitch.values = {-500, 0, 500};
  patch.macros.pitch.release = 1;
  patch.macros.algorithm.values = {5, 4, 3};

  // Op macros
  patch.operator_macros[0].tl.values = {127, 100, 80};
  patch.operator_macros[0].tl.loop = 0;
  patch.operator_macros[2].ml.values = {4, 8, 12, 1};
  patch.operator_macros[2].ml.speed = 3;

  // FUI roundtrip first (to normalize DT 0→4 via linear encoding)
  auto fui_ser = fui::serialize(patch);
  ASSERT_TRUE(is_ok(fui_ser));
  auto fui_parsed = fui::parse(get_ok(fui_ser).data(), get_ok(fui_ser).size(), "cross_macro");
  ASSERT_TRUE(is_ok(fui_parsed));
  const auto &fui_patch = get_ok(fui_parsed).patches[0];

  // FUI → GIN
  auto gin_ser = gin::serialize(fui_patch);
  ASSERT_TRUE(is_ok(gin_ser));

  // GIN → Patch
  auto gin_parsed = gin::parse(get_ok(gin_ser).data(), get_ok(gin_ser).size(), "cross_macro");
  ASSERT_TRUE(is_ok(gin_parsed));
  const auto &gin_patch = get_ok(gin_parsed).patches[0];

  // GIN → FUI
  auto fui_ser2 = fui::serialize(gin_patch);
  ASSERT_TRUE(is_ok(fui_ser2));
  auto fui_parsed2 = fui::parse(get_ok(fui_ser2).data(), get_ok(fui_ser2).size(), "cross_macro");
  ASSERT_TRUE(is_ok(fui_parsed2));
  const auto &final_patch = get_ok(fui_parsed2).patches[0];

  // Compare FUI→GIN→FUI result with original FUI result
  ASSERT_TRUE(patches_equal(fui_patch, final_patch));

  // Channel macros
  ASSERT_TRUE(macros_equal(fui_patch.macros.volume, final_patch.macros.volume));
  ASSERT_TRUE(macros_equal(fui_patch.macros.pitch, final_patch.macros.pitch));
  ASSERT_TRUE(macros_equal(fui_patch.macros.algorithm, final_patch.macros.algorithm));

  // Op macros
  ASSERT_TRUE(macros_equal(fui_patch.operator_macros[0].tl, final_patch.operator_macros[0].tl));
  ASSERT_TRUE(macros_equal(fui_patch.operator_macros[2].ml, final_patch.operator_macros[2].ml));

  // Unused macros empty
  ASSERT_TRUE(final_patch.macros.arpeggio.empty());
  ASSERT_TRUE(final_patch.operator_macros[1].tl.empty());

  return true;
}

// ---- MML pitch macro output tests ----

/// Verify that MML serialize outputs @M comment for pitch macros.
bool test_mml_pitch_macro_output() {
  Patch patch;
  patch.name = "pitch_mml_test";
  patch.algorithm = 2;
  patch.feedback = 3;
  patch.left = true;
  patch.right = true;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 0; op.ml = 1; op.dt = 4;
  }

  // Linear slope: -512, -256, 0, 256, 512 → -2>2:5 in semitones
  patch.macros.pitch.values = {-512, -256, 0, 256, 512};
  patch.macros.pitch.speed = 1;

  auto result = ctrmml::serialize_text(patch);
  ASSERT_TRUE(is_ok(result));
  const auto &text = get_ok(result);

  // Must contain @M comment
  ASSERT_TRUE(text.find("; @M1 ") != std::string::npos);
  // Must contain "pitch"
  ASSERT_TRUE(text.find("; pitch") != std::string::npos);
  // Linear slope -2>2:5
  ASSERT_TRUE(text.find("-2>2:5") != std::string::npos);

  return true;
}

/// Test @M output with loop marker and constant hold.
bool test_mml_pitch_macro_loop_and_hold() {
  Patch patch;
  patch.name = "loop_test";
  patch.algorithm = 0;
  patch.feedback = 0;
  patch.left = true;
  patch.right = true;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 0; op.ml = 1; op.dt = 4;
  }

  // Slide up then hold: -256, 0 (slope), then 0, 0, 0 (hold with loop)
  patch.macros.pitch.values = {-256, 0, 0, 0, 0};
  patch.macros.pitch.loop = 2;  // Loop at index 2 (the constant 0 section)
  patch.macros.pitch.speed = 1;

  auto result = ctrmml::serialize_text(patch);
  ASSERT_TRUE(is_ok(result));
  const auto &text = get_ok(result);

  // Slope: -1>0:2
  ASSERT_TRUE(text.find("-1>0:2") != std::string::npos);
  // Loop marker
  ASSERT_TRUE(text.find("|") != std::string::npos);
  // Constant hold: 0:3
  ASSERT_TRUE(text.find("0:3") != std::string::npos);

  return true;
}

/// Test @M output with speed > 1 (ticks are multiplied).
bool test_mml_pitch_macro_speed() {
  Patch patch;
  patch.name = "speed_test";
  patch.algorithm = 0;
  patch.feedback = 0;
  patch.left = true;
  patch.right = true;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 0; op.ml = 1; op.dt = 4;
  }

  // 3 values with speed=2 → ticks should be doubled
  patch.macros.pitch.values = {0, 256, 512};
  patch.macros.pitch.speed = 2;

  auto result = ctrmml::serialize_text(patch);
  ASSERT_TRUE(is_ok(result));
  const auto &text = get_ok(result);

  // 3 values × speed 2 = 6 ticks: 0>2:6
  ASSERT_TRUE(text.find("0>2:6") != std::string::npos);

  return true;
}

/// No pitch macro → no @M comment.
bool test_mml_no_pitch_macro() {
  Patch patch;
  patch.name = "no_pitch";
  patch.algorithm = 0;
  patch.feedback = 0;
  patch.left = true;
  patch.right = true;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.operators[i];
    op.ar = 20; op.dr = 10; op.sr = 5; op.rr = 8;
    op.sl = 7; op.tl = 30; op.ks = 0; op.ml = 1; op.dt = 4;
  }

  // Volume macro present, but no pitch macro
  patch.macros.volume.values = {15, 14, 13};

  auto result = ctrmml::serialize_text(patch);
  ASSERT_TRUE(is_ok(result));
  const auto &text = get_ok(result);

  // Must NOT contain @M
  ASSERT_TRUE(text.find("@M") == std::string::npos);

  return true;
}

// ---- High-level API tests ----

bool test_converter_parse_serialize() {
  auto bytes = read_file(fs::path(TEST_DATA_DIR) / "bright piano.dmp");
  ASSERT_TRUE(!bytes.empty());

  // Parse via high-level API with format hint
  auto result = parse(bytes.data(), bytes.size(), Format::Dmp, "bright piano");
  ASSERT_TRUE(is_ok(result));
  const auto &original = get_ok(result).patches[0];

  // Serialize to each writable format via high-level API
  Format writable[] = {Format::Dmp, Format::Fui, Format::Gin};
  for (auto fmt : writable) {
    auto ser = serialize(fmt, original);
    ASSERT_TRUE(is_ok(ser));

    auto re_parsed = parse(get_ok(ser).data(), get_ok(ser).size(), fmt, "bright piano");
    ASSERT_TRUE(is_ok(re_parsed));

    if (!patches_equal(original, get_ok(re_parsed).patches[0])) {
      std::cerr << "  High-level roundtrip failed for format "
                << format_to_extension(fmt) << ":\n";
      print_patch_diff(original, get_ok(re_parsed).patches[0]);
      return false;
    }
  }

  return true;
}

// ---- Main ----

int main() {
  std::cout << "=== Detune conversion ===\n";
  RUN_TEST(test_detune_linear_to_hardware);
  RUN_TEST(test_detune_hardware_to_linear);
  RUN_TEST(test_detune_roundtrip_linear);
  RUN_TEST(test_detune_roundtrip_hardware);

  std::cout << "\n=== DMP parse ===\n";
  RUN_TEST(test_dmp_parse_bright_piano);
  RUN_TEST(test_dmp_parse_organ);
  RUN_TEST(test_dmp_parse_acoustic_bass);

  std::cout << "\n=== DMP roundtrip ===\n";
  RUN_TEST(test_dmp_roundtrip);

  std::cout << "\n=== Cross-format roundtrip ===\n";
  RUN_TEST(test_dmp_to_fui_roundtrip);
  RUN_TEST(test_dmp_to_gin_roundtrip);
  RUN_TEST(test_dmp_to_mml_roundtrip);

  std::cout << "\n=== DMF tests ===\n";
  RUN_TEST(test_dmf_parse_detune);
  RUN_TEST(test_dmf_to_fui_roundtrip);
  RUN_TEST(test_dmf_to_dmp_roundtrip);

  std::cout << "\n=== Exhaustive detune ===\n";
  RUN_TEST(test_all_detune_values_roundtrip);

  std::cout << "\n=== Full parameter roundtrip ===\n";
  RUN_TEST(test_full_parameter_roundtrip);

  std::cout << "\n=== FUI macro roundtrip ===\n";
  RUN_TEST(test_fui_channel_macro_roundtrip);
  RUN_TEST(test_fui_operator_macro_roundtrip);
  RUN_TEST(test_fui_macro_i32_values);
  RUN_TEST(test_fui_no_macro_roundtrip);
  RUN_TEST(test_fui_all_channel_macro_codes);

  std::cout << "\n=== GIN macro roundtrip ===\n";
  RUN_TEST(test_gin_macro_roundtrip);
  RUN_TEST(test_gin_no_macro_backward_compat);
  RUN_TEST(test_fui_to_gin_macro_roundtrip);

  std::cout << "\n=== MML pitch macro ===\n";
  RUN_TEST(test_mml_pitch_macro_output);
  RUN_TEST(test_mml_pitch_macro_loop_and_hold);
  RUN_TEST(test_mml_pitch_macro_speed);
  RUN_TEST(test_mml_no_pitch_macro);

  std::cout << "\n=== High-level API ===\n";
  RUN_TEST(test_converter_parse_serialize);

  std::cout << "\n" << pass_count << "/" << test_count << " tests passed\n";
  return (pass_count == test_count) ? 0 : 1;
}
