# ym2612_format

A library and CLI for converting YM2612 FM instrument patch files between formats.

## Supported formats

| Extension  | Name               | Read | Write | Notes                 |
| ---------- | ------------------ | :--: | :---: | --------------------- |
| `.dmp`     | DefleMask Preset   |  o   |   o   |                       |
| `.fui`     | Furnace Instrument |  o   |   o   | FINS + legacy         |
| `.rym2612` | RYM2612 Preset     |  o   |       | XML                   |
| `.mml`     | ctrmml (MML)       |  o   |   o   | Text                  |
| `.gin`     | GIN (JSON)         |  o   |   o   |                       |
| `.ginpkg`  | GINPKG (ZIP)       |  o   |       | Extracts all versions |

> **Note:** Only FM instrument parameters are supported. Instrument macros (volume, arpeggio, duty, etc.) are not handled.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Dependencies (nlohmann/json, miniz) are fetched automatically via CMake FetchContent.

### Options

| Variable                    | Default | Description        |
| --------------------------- | ------- | ------------------ |
| `YM2612_FORMAT_BUILD_CLI`   | `ON`    | Build the CLI tool |
| `YM2612_FORMAT_BUILD_TESTS` | `OFF`   | Build tests        |

## CLI

```sh
# List supported formats
./build/ym2612_convert formats

# Show patch info
./build/ym2612_convert info input.dmp

# Convert
./build/ym2612_convert convert input.dmp -o output.fui

# Force format
./build/ym2612_convert convert input.bin -o output.bin -f dmp
```

Files containing multiple patches (MML, GINPKG) are written as separate output files.

## Library usage

```cpp
#include <ym2612_format/ym2612_format.hpp>

using namespace ym2612_format;

// Parse with auto-detection (hint by format enum)
auto result = parse(data, size, Format::Dmp);

// Parse with a specific format
auto result = dmp::parse(data, size, "my_patch");

// Serialize to a different format
auto bytes = fui::serialize(patch);

// High-level API with Format enum
auto bytes = serialize(Format::Fui, patch);
```

CMake integration:

```cmake
add_subdirectory(ym2612_format)
target_link_libraries(your_target PRIVATE ym2612_format)
```
