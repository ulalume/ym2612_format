#include "ym2612_format/converter.hpp"

#include "ym2612_format/ctrmml.hpp"
#include "ym2612_format/dmf.hpp"
#include "ym2612_format/dmp.hpp"
#include "ym2612_format/eif.hpp"
#include "ym2612_format/fui.hpp"
#include "ym2612_format/fur.hpp"
#include "ym2612_format/gin.hpp"
#include "ym2612_format/ginpkg.hpp"
#include "ym2612_format/opm.hpp"
#include "ym2612_format/rym2612.hpp"
#include "ym2612_format/tfi.hpp"
#include "ym2612_format/vgi.hpp"
#include "ym2612_format/vgm.hpp"

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
      {"vgi", Format::Vgi},
      {"eif", Format::Eif},
      {"vgm", Format::Vgm},
      {"vgz", Format::Vgm},
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
  case Format::Vgi:     return "vgi";
  case Format::Eif:     return "eif";
  case Format::Vgm:     return "vgm";
  }
  return "";
}

// --- Internal format registry ---

namespace {

struct FormatEntry {
  FormatInfo info;
  ParseResult (*parse)(const uint8_t *, size_t, const std::string &);
  /// Lenient variant used when the caller explicitly names this format
  /// (hint or parse_as); sniffing always uses the strict parse.
  /// nullptr when the module has no separate compatibility parser.
  ParseResult (*parse_compatible)(const uint8_t *, size_t,
                                  const std::string &);
  SerializeResult (*serialize)(const Patch &);         // nullptr if read-only
  SerializeTextResult (*serialize_text)(const Patch &); // nullptr if N/A
};

SerializeResult ctrmml_serialize_wrapper(const Patch &p) {
  return ctrmml::serialize(p);
}

SerializeTextResult ctrmml_serialize_text_wrapper(const Patch &p) {
  return ctrmml::serialize_text(p);
}

/// Build a registry entry from a module's info().  The capability
/// flags are derived from what is actually wired here, so the listing
/// can never disagree with what parse_as()/serialize() will accept.
FormatEntry make_entry(FormatInfo info,
                       ParseResult (*parse)(const uint8_t *, size_t,
                                            const std::string &),
                       SerializeResult (*serialize)(const Patch &),
                       SerializeTextResult (*serialize_text)(const Patch &),
                       ParseResult (*parse_compatible)(const uint8_t *, size_t,
                                                       const std::string &) =
                           nullptr) {
  info.can_read = parse != nullptr;
  info.can_write = serialize != nullptr;
  return {std::move(info), parse, parse_compatible, serialize, serialize_text};
}

const std::vector<FormatEntry> &formats() {
  // Ordering rule for hint-less auto-detection: entries with magic
  // bytes come first, magic-less size/range-validated sniffers (tfi,
  // vgi, eif, dmp) after them.  Dmp stays last as the loosest of the
  // magic-less sniffers.  Keep new formats above it.
  static const std::vector<FormatEntry> entries = {
      make_entry(vgm::info(), vgm::parse, nullptr, nullptr),
      make_entry(dmf::info(), dmf::parse, nullptr, nullptr),
      make_entry(fui::info(), fui::parse, fui::serialize, nullptr),
      make_entry(gin::info(), gin::parse, gin::serialize, nullptr),
      make_entry(ginpkg::info(), ginpkg::parse, nullptr, nullptr),
      make_entry(rym2612::info(), rym2612::parse, nullptr, nullptr),
      make_entry(ctrmml::info(), ctrmml::parse, ctrmml_serialize_wrapper,
                 ctrmml_serialize_text_wrapper),
      make_entry(fur::info(), fur::parse, nullptr, nullptr),
      make_entry(opm::info(), opm::parse, nullptr, nullptr),
      make_entry(tfi::info(), tfi::parse, tfi::serialize, nullptr,
                 tfi::parse_compatible),
      make_entry(vgi::info(), vgi::parse, vgi::serialize, nullptr),
      make_entry(eif::info(), eif::parse, eif::serialize, nullptr),
      // Loosest magic-less sniffer — stays last (see ordering rule).
      make_entry(dmp::info(), dmp::parse, dmp::serialize, nullptr,
                 dmp::parse_compatible),
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

  // Try the hinted format first.  An explicit format choice is
  // authoritative, so prefer the module's lenient compatibility parser
  // over the strict sniffing one when it provides both.
  std::optional<Error> hint_error;
  if (hint) {
    if (auto *entry = find_entry(*hint)) {
      auto *parse_fn =
          entry->parse_compatible ? entry->parse_compatible : entry->parse;
      auto result = parse_fn(data, size, name);
      if (is_ok(result))
        return result;
      hint_error = get_error(result);
    }
  }

  // Try all formats (the hinted one already failed above — skip it)
  for (const auto &entry : formats()) {
    if (hint && entry.info.format == *hint)
      continue;
    auto result = entry.parse(data, size, name);
    if (is_ok(result))
      return result;
  }

  // Nothing matched: the hinted format's own error is more useful
  // than the generic message (e.g. a corrupt .vgz reports its
  // decompression failure).
  if (hint_error)
    return *hint_error;
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
  // Same rule as the hint path in parse(): an explicit format choice
  // gets the lenient compatibility parser when the module has one.
  auto *parse_fn =
      entry->parse_compatible ? entry->parse_compatible : entry->parse;
  return parse_fn(data, size, name);
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
