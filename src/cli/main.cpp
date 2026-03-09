#include "ym2612_format/ym2612_format.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ym2612_format;

namespace {

void print_usage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog << " convert <input> -o <output> [-f <format>]\n"
            << "  " << prog << " info <input>\n"
            << "  " << prog << " formats\n"
            << "\n"
            << "Commands:\n"
            << "  convert   Convert between YM2612 patch formats\n"
            << "  info      Display patch information\n"
            << "  formats   List supported formats\n";
}

std::vector<uint8_t> read_file(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  return std::vector<uint8_t>{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
}

bool write_file(const fs::path &path, const std::vector<uint8_t> &data) {
  std::ofstream file(path, std::ios::binary);
  if (!file)
    return false;
  file.write(reinterpret_cast<const char *>(data.data()),
             static_cast<std::streamsize>(data.size()));
  return file.good();
}

std::string extension_of(const fs::path &path) {
  auto ext = path.extension().string();
  if (!ext.empty() && ext.front() == '.')
    ext = ext.substr(1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext;
}

void print_patch(const Patch &patch, size_t index = 0) {
  std::cout << "--- Patch";
  if (index > 0)
    std::cout << " #" << index;
  std::cout << " ---\n";
  std::cout << "Name:      " << patch.name << "\n";
  std::cout << "Algorithm: " << static_cast<int>(patch.algorithm) << "\n";
  std::cout << "Feedback:  " << static_cast<int>(patch.feedback) << "\n";
  std::cout << "AMS:       " << static_cast<int>(patch.ams) << "\n";
  std::cout << "FMS:       " << static_cast<int>(patch.fms) << "\n";
  std::cout << "LFO:       " << (patch.lfo_enable ? "on" : "off")
            << " freq=" << static_cast<int>(patch.lfo_frequency) << "\n";
  std::cout << "Panning:   L=" << (patch.left ? "on" : "off")
            << " R=" << (patch.right ? "on" : "off") << "\n";

  for (int i = 0; i < 4; ++i) {
    const auto &op = patch.operators[i];
    std::cout << "  OP" << (i + 1) << ": "
              << "AR=" << static_cast<int>(op.ar)
              << " DR=" << static_cast<int>(op.dr)
              << " SR=" << static_cast<int>(op.sr)
              << " RR=" << static_cast<int>(op.rr)
              << " SL=" << static_cast<int>(op.sl)
              << " TL=" << static_cast<int>(op.tl)
              << " KS=" << static_cast<int>(op.ks)
              << " ML=" << static_cast<int>(op.ml)
              << " DT=" << static_cast<int>(op.dt);
    if (op.ssg_enable)
      std::cout << " SSG=" << static_cast<int>(op.ssg);
    if (op.am)
      std::cout << " AM";
    if (!op.enable)
      std::cout << " (disabled)";
    std::cout << "\n";
  }
}

int cmd_formats() {
  auto fmts = all_formats();
  std::cout << "Supported formats:\n\n";
  for (const auto &f : fmts) {
    std::cout << "  ." << f.extension << "  " << f.name;
    std::cout << "  [";
    if (f.can_read) std::cout << "read";
    if (f.can_read && f.can_write) std::cout << "/";
    if (f.can_write) std::cout << "write";
    std::cout << "]";
    if (f.is_text) std::cout << " (text)";
    std::cout << "\n";
  }
  return 0;
}

int cmd_info(const fs::path &input_path) {
  auto bytes = read_file(input_path);
  if (bytes.empty()) {
    std::cerr << "Error: could not read " << input_path << "\n";
    return 1;
  }

  auto hint = format_from_string(extension_of(input_path));
  auto stem = input_path.stem().string();
  auto result = parse(bytes.data(), bytes.size(), hint, stem);

  if (!is_ok(result)) {
    std::cerr << "Error: " << get_error(result).message << "\n";
    return 1;
  }

  const auto &ok = get_ok(result);
  for (const auto &w : ok.warnings)
    std::cerr << "Warning: " << w << "\n";

  for (size_t i = 0; i < ok.patches.size(); ++i)
    print_patch(ok.patches[i], ok.patches.size() > 1 ? i + 1 : 0);

  return 0;
}

int cmd_convert(const fs::path &input_path, const fs::path &output_path,
                const std::string &force_format) {
  auto bytes = read_file(input_path);
  if (bytes.empty()) {
    std::cerr << "Error: could not read " << input_path << "\n";
    return 1;
  }

  auto in_hint = format_from_string(extension_of(input_path));
  auto stem = input_path.stem().string();
  auto result = parse(bytes.data(), bytes.size(), in_hint, stem);

  if (!is_ok(result)) {
    std::cerr << "Error: " << get_error(result).message << "\n";
    return 1;
  }

  const auto &ok = get_ok(result);
  for (const auto &w : ok.warnings)
    std::cerr << "Warning: " << w << "\n";

  if (ok.patches.empty()) {
    std::cerr << "Error: no patches found\n";
    return 1;
  }

  std::string out_ext_str =
      force_format.empty() ? extension_of(output_path) : force_format;
  auto out_format = format_from_string(out_ext_str);

  if (!out_format) {
    std::cerr << "Error: unknown output format '" << out_ext_str << "'\n";
    return 1;
  }

  // For multi-patch inputs, convert each patch to a separate file.
  // Use the patch name as the filename suffix when available.
  if (ok.patches.size() > 1) {
    auto dir = output_path.parent_path();
    auto ext = output_path.extension().string();

    int errors = 0;
    for (size_t i = 0; i < ok.patches.size(); ++i) {
      auto ser = serialize(*out_format, ok.patches[i]);
      if (!is_ok(ser)) {
        std::cerr << "Error serializing patch " << (i + 1) << ": "
                  << get_error(ser).message << "\n";
        ++errors;
        continue;
      }
      // Use patch name for filename; fall back to index number
      std::string file_stem = ok.patches[i].name;
      if (file_stem.empty())
        file_stem = output_path.stem().string() + "_" + std::to_string(i + 1);
      auto named_path = dir / (file_stem + ext);
      if (!write_file(named_path, get_ok(ser))) {
        std::cerr << "Error writing " << named_path << "\n";
        ++errors;
      } else {
        std::cout << named_path.filename().string() << "\n";
      }
    }
    return errors > 0 ? 1 : 0;
  }

  auto ser = serialize(*out_format, ok.patches[0]);
  if (!is_ok(ser)) {
    std::cerr << "Error: " << get_error(ser).message << "\n";
    return 1;
  }

  if (!write_file(output_path, get_ok(ser))) {
    std::cerr << "Error: could not write " << output_path << "\n";
    return 1;
  }

  std::cout << output_path.filename().string() << "\n";
  return 0;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  std::string command = argv[1];

  if (command == "formats") {
    return cmd_formats();
  }

  if (command == "info") {
    if (argc < 3) {
      std::cerr << "Usage: " << argv[0] << " info <input>\n";
      return 1;
    }
    return cmd_info(argv[2]);
  }

  if (command == "convert") {
    if (argc < 3) {
      std::cerr << "Usage: " << argv[0]
                << " convert <input> -o <output> [-f <format>]\n";
      return 1;
    }

    fs::path input_path = argv[2];
    fs::path output_path;
    std::string force_format;

    for (int i = 3; i < argc; ++i) {
      if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
        output_path = argv[++i];
      } else if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
        force_format = argv[++i];
      }
    }

    if (output_path.empty()) {
      std::cerr << "Error: -o <output> is required\n";
      return 1;
    }

    return cmd_convert(input_path, output_path, force_format);
  }

  print_usage(argv[0]);
  return 1;
}
