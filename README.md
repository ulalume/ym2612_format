# ym2612_format

A library and CLI for converting YM2612 FM instrument patch files between formats.

## Supported formats

| Extension  | Name               | Read | Write | Macros | Notes                                      |
| ---------- | ------------------ | :--: | :---: | :----: | ------------------------------------------ |
| `.dmp`     | DefleMask Preset   |  o   |   o   |        |                                            |
| `.dmf`     | DefleMask Module   |  o   |       |        | Extracts FM instruments                    |
| `.fui`     | Furnace Instrument |  o   |   o   |   o    | FINS + legacy                              |
| `.fur`     | Furnace Module     |  o   |       |   o    | Extracts FM instruments                    |
| `.rym2612` | RYM2612 Preset     |  o   |       |        | XML                                        |
| `.mml`     | ctrmml (MML)       |  o   |   o   |        | Pitch macro output as `@M`                 |
| `.opm`     | VOPM / MiOPMdrv    |  o   |       |        | OPM → OPN2; DT2/AMD/PMD/WF/NE/SLOT dropped |
| `.tfi`     | TFM Music Maker    |  o   |   o   |        | 42-byte YM2203/OPN2 FM patch               |
| `.vgi`     | VGM Music Maker    |  o   |   o   |        | 43-byte OPN2 FM patch (TFI + FMS/AMS/AM)   |
| `.eif`     | Echo (EIF)         |  o   |   o   |        | 29-byte raw register dump                  |
| `.vgm` / `.vgz` | VGM register log | o |       |        | Reconstructs FM instruments from YM2612 writes |
| `.gin`     | GIN (JSON)         |  o   |   o   |   o    |                                            |
| `.ginpkg`  | GINPKG (ZIP)       |  o   |       |        | Extracts all versions                      |

Instrument macros (volume, arpeggio, pitch, per-operator TL/AR, etc.) are read/written in FUI, FUR, and GIN. MML export includes pitch macros as commented `@M` definitions.

`.vgm` / `.vgz` are register logs; only instruments actually played appear.

Byte layouts and per-format serialization limits are documented in each
header under `include/ym2612_format/`.

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

# Extract instruments from a VGM/VGZ register log
# (multiple patches → written into the directory ./instruments/)
./build/ym2612_convert convert song.vgz -o instruments.vgi
```

Files containing multiple patches (DMF, FUR, MML, OPM, GINPKG, VGM) are written as separate output files.

## Library usage

```cpp
#include <ym2612_format/ym2612_format.hpp>

using namespace ym2612_format;

// Parse a file identified as DMP (also accepts truncated legacy presets)
auto result = parse(data, size, Format::Dmp);

// Strict low-level DMP parser, suitable for format sniffing
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

## Credits

The VGM instrument-extraction approach (key-on snapshots, volume-variant
grouping) is ported from [vgm2pre](https://github.com/vgmtool/vgm2pre)
by Alex Rosario (MIT license), itself based on Shiru's VGM2TFI.
