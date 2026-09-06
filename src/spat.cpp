#include "ym2612_format/spat.hpp"

#include "ym2612_format/eif.hpp"

namespace ym2612_format::spat {

namespace {

constexpr size_t kFileSize = 32;
constexpr size_t kEifSize = 29;

} // namespace

FormatInfo info() {
  return {Format::Spat, "Sona", "spat", true, true, false};
}

ParseResult parse(const uint8_t *data, size_t size,
                  const std::string &fallback_name) {
  if (!data || size == 0)
    return Error{"Empty data"};
  if (size != kFileSize)
    return Error{"Not a SonaPatch file (expected 32 bytes)"};
  for (size_t i = kEifSize; i < kFileSize; ++i) {
    if (data[i] != 0)
      return Error{"Not a SonaPatch file (reserved bytes must be 0)"};
  }

  auto result = eif::parse(data, kEifSize, fallback_name);
  if (!is_ok(result))
    return Error{"Not a SonaPatch file (invalid register ranges)"};
  return result;
}

SerializeResult serialize(const Patch &patch) {
  auto result = eif::serialize(patch);
  if (!is_ok(result))
    return get_error(result);

  auto data = get_ok(result);
  data.resize(kFileSize, 0);
  return data;
}

} // namespace ym2612_format::spat
