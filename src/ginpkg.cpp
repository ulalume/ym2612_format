#include "ym2612_format/ginpkg.hpp"

#include "ym2612_format/gin.hpp"
#include "json_minimal.hpp"
#include <algorithm>
#include <miniz.h>
#include <string>
#include <vector>

namespace ym2612_format::ginpkg {

FormatInfo info() {
  return {Format::Ginpkg, "GINPKG (ZIP)", "ginpkg", true, false, false};
}

namespace {

using Json = json_minimal::Value;

struct ZipGuard {
  mz_zip_archive *a;
  ~ZipGuard() { mz_zip_reader_end(a); }
};

/// Sanitize ISO8601 timestamp for use in filenames (replace : with -)
std::string sanitize_timestamp(const std::string &ts) {
  std::string s = ts;
  std::replace(s.begin(), s.end(), ':', '-');
  return s;
}

/// Extract a ZIP entry to a string. Returns empty optional on failure.
std::optional<std::string> read_entry(mz_zip_archive &archive,
                                      const std::string &name) {
  size_t size = 0;
  void *buf =
      mz_zip_reader_extract_file_to_heap(&archive, name.c_str(), &size, 0);
  if (!buf)
    return std::nullopt;
  std::string result(static_cast<const char *>(buf), size);
  mz_free(buf);
  return result;
}

} // namespace

ParseResult parse(const uint8_t *data, size_t size, const std::string &name) {
  if (!data || size == 0)
    return Error{"Empty data"};

  mz_zip_archive archive{};
  if (!mz_zip_reader_init_mem(&archive, data, size, 0))
    return Error{"Failed to open GINPKG as ZIP"};
  ZipGuard guard{&archive};

  // Read current.gin (required)
  auto current_data = read_entry(archive, "current.gin");
  if (!current_data)
    return Error{"GINPKG missing current.gin"};

  std::vector<Patch> patches;
  std::vector<std::string> warnings;

  // Read history.json to get timestamps and snapshot UUIDs
  std::string current_timestamp;
  struct HistoryEntry {
    std::string uuid;
    std::string timestamp;
  };
  std::vector<HistoryEntry> history;

  auto history_data = read_entry(archive, "history.json");
  if (history_data) {
    try {
      auto j = Json::parse(*history_data);

      // Current timestamp
      if (j.contains("current") && j.at("current").is_object()) {
        const auto &cur = j.at("current");
        if (cur.contains("timestamp") && cur.at("timestamp").is_string())
          current_timestamp = cur.at("timestamp").get_string();
      }

      // Version entries
      if (j.contains("versions") && j.at("versions").is_array()) {
        for (const auto &v : j.at("versions")) {
          std::string uuid, ts;
          if (v.contains("uuid") && v.at("uuid").is_string())
            uuid = v.at("uuid").get_string();
          if (v.contains("timestamp") && v.at("timestamp").is_string())
            ts = v.at("timestamp").get_string();
          if (!uuid.empty() && !ts.empty())
            history.push_back({uuid, ts});
        }
      }
    } catch (const std::exception &e) {
      warnings.push_back(std::string("Failed to parse history.json: ") +
                          e.what());
    }
  }

  // Parse current.gin
  auto current_result = gin::parse(
      reinterpret_cast<const uint8_t *>(current_data->data()),
      current_data->size(), name);
  if (is_ok(current_result)) {
    auto &ok = get_ok(current_result);
    for (auto patch : ok.patches) {
      // Tag the name with the current timestamp
      std::string ts =
          current_timestamp.empty() ? "current" : current_timestamp;
      patch.name = patch.name + "_" + sanitize_timestamp(ts);
      patches.push_back(std::move(patch));
    }
    warnings.insert(warnings.end(), ok.warnings.begin(), ok.warnings.end());
  } else {
    return Error{"Failed to parse current.gin: " +
                  get_error(current_result).message};
  }

  // Parse each historical snapshot (newest first in history)
  for (const auto &entry : history) {
    std::string snapshot_name = entry.uuid + ".gin";
    auto snapshot_data = read_entry(archive, snapshot_name);
    if (!snapshot_data) {
      warnings.push_back("Missing snapshot " + snapshot_name);
      continue;
    }

    auto snap_result = gin::parse(
        reinterpret_cast<const uint8_t *>(snapshot_data->data()),
        snapshot_data->size(), name);
    if (is_ok(snap_result)) {
      auto &ok = get_ok(snap_result);
      for (auto patch : ok.patches) {
        patch.name = patch.name + "_" + sanitize_timestamp(entry.timestamp);
        patches.push_back(std::move(patch));
      }
    } else {
      warnings.push_back("Failed to parse snapshot " + snapshot_name);
    }
  }

  return ParseOk{std::move(patches), std::move(warnings)};
}

} // namespace ym2612_format::ginpkg
