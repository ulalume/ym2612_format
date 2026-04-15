#include "ym2612_format/converter.hpp"

#include "ym2612_format/ctrmml.hpp"
#include "ym2612_format/dmf.hpp"
#include "ym2612_format/dmp.hpp"
#include "ym2612_format/fui.hpp"
#include "ym2612_format/fur.hpp"
#include "ym2612_format/gin.hpp"
#include "ym2612_format/ginpkg.hpp"
#include "ym2612_format/opm.hpp"
#include "ym2612_format/rym2612.hpp"
#include "ym2612_format/tfi.hpp"

#include <algorithm>
#include <unordered_map>

namespace ym2612_format {

// --- Format ↔ string conversion ---

std::optional<Format> format_from_string(const std::string &s) {
  std::string lower = s;
  if (!lower.empty() && lower.front() == '.')
    lower = lower.substr(1);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  static const std::unordered_map<std::string, Format> map = {
      {"dmp", Format::Dmp},       {"dmf", Format::Dmf},
      {"fui", Format::Fui},       {"gin", Format::Gin},
      {"ginpkg", Format::Ginpkg}, {"rym2612", Format::Rym2612},
      {"mml", Format::Mml},
      {"fur", Format::Fur},
      {"opm", Format::Opm},
      {"tfi", Format::Tfi},
  };
  auto it = map.find(lower);
  if (it != map.end())
    return it->second;
  return std::nullopt;
}

const char *format_to_extension(Format f) {
  switch (f) {
  case Format::Dmp:     return "dmp";
  case Format::Dmf:     return "dmf";
  case Format::Fui:     return "fui";
  case Format::Gin:     return "gin";
  case Format::Ginpkg:  return "ginpkg";
  case Format::Rym2612: return "rym2612";
  case Format::Mml:     return "mml";
  case Format::Fur:     return "fur";
  case Format::Opm:     return "opm";
  case Format::Tfi:     return "tfi";
  }
  return "";
}

// --- Internal format registry ---

namespace {

struct FormatEntry {
  FormatInfo info;
  ParseResult (*parse)(const uint8_t *, size_t, const std::string &);
  SerializeResult (*serialize)(const Patch &);         // nullptr if read-only
  SerializeTextResult (*serialize_text)(const Patch &); // nullptr if N/A
};

SerializeResult ctrmml_serialize_wrapper(const Patch &p) {
  return ctrmml::serialize(p);
}

SerializeTextResult ctrmml_serialize_text_wrapper(const Patch &p) {
  return ctrmml::serialize_text(p);
}

FormatInfo make_info(Format f,
                     const char *name, const char *ext,
                     bool read, bool write, bool text = false) {
  return {f, name, ext, read, write, text};
}

const std::vector<FormatEntry> &formats() {
  static const std::vector<FormatEntry> entries = {
      {make_info(Format::Dmp, "DefleMask Preset", "dmp", true, true),
       dmp::parse, dmp::serialize, nullptr},
      {make_info(Format::Dmf, "DefleMask Module", "dmf", true, false),
       dmf::parse, nullptr, nullptr},
      {make_info(Format::Fui, "Furnace Instrument", "fui", true, true),
       fui::parse, fui::serialize, nullptr},
      {make_info(Format::Gin, "GIN (JSON)", "gin", true, true),
       gin::parse, gin::serialize, nullptr},
      {make_info(Format::Ginpkg, "GINPKG (ZIP)", "ginpkg", true, false),
       ginpkg::parse, nullptr, nullptr},
      {make_info(Format::Rym2612, "RYM2612 Preset", "rym2612", true, false),
       rym2612::parse, nullptr, nullptr},
      {make_info(Format::Mml, "ctrmml (MML)", "mml", true, true, true),
       ctrmml::parse, ctrmml_serialize_wrapper, ctrmml_serialize_text_wrapper},
      {make_info(Format::Fur, "Furnace Module", "fur", true, false),
       fur::parse, nullptr, nullptr},
      {make_info(Format::Opm, "VOPM/MiOPMdrv", "opm", true, false, true),
       opm::parse, nullptr, nullptr},
      {make_info(Format::Tfi, "TFM Music Maker (TFI)", "tfi", true, true),
       tfi::parse, tfi::serialize, nullptr},
  };
  return entries;
}

const FormatEntry *find_entry(Format f) {
  for (const auto &entry : formats()) {
    if (entry.info.format == f)
      return &entry;
  }
  return nullptr;
}

} // namespace

// --- Public API ---

std::vector<FormatInfo> all_formats() {
  std::vector<FormatInfo> result;
  for (const auto &entry : formats())
    result.push_back(entry.info);
  return result;
}

ParseResult parse(const uint8_t *data, size_t size,
                  std::optional<Format> hint, const std::string &name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  // Try the hinted format first
  if (hint) {
    if (auto *entry = find_entry(*hint)) {
      auto result = entry->parse(data, size, name);
      if (is_ok(result))
        return result;
    }
  }

  // Try all formats
  for (const auto &entry : formats()) {
    auto result = entry.parse(data, size, name);
    if (is_ok(result))
      return result;
  }

  return Error{"Unable to detect format"};
}

ParseResult parse_as(Format format, const uint8_t *data, size_t size,
                     const std::string &name) {
  auto *entry = find_entry(format);
  if (!entry)
    return Error{"Unknown format"};
  if (!entry->info.can_read)
    return Error{std::string("Format '") + format_to_extension(format) +
                 "' does not support reading"};
  return entry->parse(data, size, name);
}

SerializeResult serialize(Format format, const Patch &patch) {
  auto *entry = find_entry(format);
  if (!entry)
    return Error{"Unknown format"};
  if (!entry->serialize)
    return Error{std::string("Format '") + format_to_extension(format) +
                 "' does not support writing"};
  return entry->serialize(patch);
}

SerializeTextResult serialize_text(Format format, const Patch &patch) {
  auto *entry = find_entry(format);
  if (!entry)
    return Error{"Unknown format"};
  if (!entry->serialize_text)
    return Error{std::string("Format '") + format_to_extension(format) +
                 "' does not support text output"};
  return entry->serialize_text(patch);
}

} // namespace ym2612_format
