#include "ym2612_format/c_api.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---- Helpers ----

static int test_count = 0;
static int pass_count = 0;

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
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

static std::vector<uint8_t> bright_piano() {
  return read_file(fs::path(TEST_DATA_DIR) / "bright piano.dmp");
}

static bool contains(const char *haystack, const char *needle) {
  return std::strstr(haystack, needle) != nullptr;
}

// ---- Tests ----

bool test_version() {
  const char *version = ym2612_version();
  ASSERT_TRUE(version != nullptr);
  ASSERT_TRUE(std::strlen(version) >= 5);
  return true;
}

bool test_formats_json() {
  char *json = ym2612_formats_json();
  ASSERT_TRUE(json != nullptr);
  bool ok = contains(json, "\"dmp\"") && contains(json, "\"can_write\"") &&
            contains(json, "\"can_read\"") && contains(json, "\"is_text\"") &&
            contains(json, "\"mml\"");
  ym2612_free(json);
  ASSERT_TRUE(ok);
  return true;
}

bool test_parse_dmp() {
  auto bytes = bright_piano();
  ASSERT_TRUE(!bytes.empty());

  char *error = nullptr;
  char *json = ym2612_parse_json(bytes.data(), bytes.size(),
                                 "bright piano.dmp", nullptr, &error);
  ASSERT_TRUE(json != nullptr);
  ASSERT_TRUE(error == nullptr);
  // The DMP format carries no name, so the fallback `name` is used verbatim.
  bool ok = contains(json, "\"patches\"") &&
            contains(json, "\"name\":\"bright piano.dmp\"") &&
            contains(json, "\"algorithm\":0") &&
            contains(json, "\"feedback\":5") &&
            contains(json, "\"has_macros\":false") &&
            contains(json, "\"mml\":\"@1 fm ") &&
            contains(json, "OP4\\n\"") && contains(json, "\"warnings\":[]");
  ym2612_free(json);
  ASSERT_TRUE(ok);
  return true;
}

bool test_parse_explicit_format() {
  auto bytes = bright_piano();
  ASSERT_TRUE(!bytes.empty());

  char *json =
      ym2612_parse_json(bytes.data(), bytes.size(), "patch", "dmp", nullptr);
  ASSERT_TRUE(json != nullptr);
  ym2612_free(json);
  return true;
}

bool test_convert_dmp_to_tfi() {
  auto bytes = bright_piano();
  ASSERT_TRUE(!bytes.empty());

  size_t size = 0;
  char *error = nullptr;
  uint8_t *out = ym2612_convert(bytes.data(), bytes.size(), "bright piano.dmp",
                                nullptr, 0, "tfi", &size, &error);
  ASSERT_TRUE(out != nullptr);
  ASSERT_TRUE(error == nullptr);
  ASSERT_TRUE(size == 42);
  ym2612_free(out);
  return true;
}

bool test_convert_to_mml() {
  auto bytes = bright_piano();
  ASSERT_TRUE(!bytes.empty());

  size_t size = 0;
  uint8_t *out = ym2612_convert(bytes.data(), bytes.size(), "bright piano.dmp",
                                nullptr, 0, "mml", &size, nullptr);
  ASSERT_TRUE(out != nullptr);
  ASSERT_TRUE(size > 0);
  std::string text(reinterpret_cast<char *>(out), size);
  ym2612_free(out);
  ASSERT_TRUE(text.rfind("@1 fm ", 0) == 0);
  return true;
}

bool test_error_unknown_format() {
  auto bytes = bright_piano();
  char *error = nullptr;
  char *json = ym2612_parse_json(bytes.data(), bytes.size(), "patch", "nope",
                                 &error);
  ASSERT_TRUE(json == nullptr);
  ASSERT_TRUE(error != nullptr);
  bool ok = contains(error, "nope");
  ym2612_free(error);
  ASSERT_TRUE(ok);
  return true;
}

bool test_error_empty_data() {
  char *error = nullptr;
  char *json = ym2612_parse_json(nullptr, 0, "patch.dmp", nullptr, &error);
  ASSERT_TRUE(json == nullptr);
  ASSERT_TRUE(error != nullptr);
  ym2612_free(error);
  return true;
}

bool test_error_index_out_of_range() {
  auto bytes = bright_piano();
  size_t size = 123;
  char *error = nullptr;
  uint8_t *out = ym2612_convert(bytes.data(), bytes.size(), "bright piano.dmp",
                                nullptr, 5, "tfi", &size, &error);
  ASSERT_TRUE(out == nullptr);
  ASSERT_TRUE(size == 0);
  ASSERT_TRUE(error != nullptr);
  bool ok = contains(error, "out of range");
  ym2612_free(error);
  ASSERT_TRUE(ok);
  return true;
}

bool test_error_unwritable_target() {
  auto bytes = bright_piano();
  size_t size = 0;
  char *error = nullptr;
  uint8_t *out = ym2612_convert(bytes.data(), bytes.size(), "bright piano.dmp",
                                nullptr, 0, "vgm", &size, &error);
  ASSERT_TRUE(out == nullptr);
  ASSERT_TRUE(error != nullptr);
  bool ok = contains(error, "writing");
  ym2612_free(error);
  ASSERT_TRUE(ok);
  return true;
}

bool test_error_without_error_pointer() {
  char *json = ym2612_parse_json(nullptr, 0, "patch.dmp", nullptr, nullptr);
  ASSERT_TRUE(json == nullptr);
  return true;
}

bool test_free_null() {
  ym2612_free(nullptr);
  return true;
}

int main() {
  std::cout << "=== C API ===\n";
  RUN_TEST(test_version);
  RUN_TEST(test_formats_json);
  RUN_TEST(test_parse_dmp);
  RUN_TEST(test_parse_explicit_format);
  RUN_TEST(test_convert_dmp_to_tfi);
  RUN_TEST(test_convert_to_mml);
  RUN_TEST(test_error_unknown_format);
  RUN_TEST(test_error_empty_data);
  RUN_TEST(test_error_index_out_of_range);
  RUN_TEST(test_error_unwritable_target);
  RUN_TEST(test_error_without_error_pointer);
  RUN_TEST(test_free_null);

  std::cout << "\n" << pass_count << "/" << test_count << " tests passed\n";
  return (pass_count == test_count) ? 0 : 1;
}
