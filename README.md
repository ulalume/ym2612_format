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

`.opm` is a YM2151 (OPM) instrument format. On import AR/D1R/D2R/RR/D1L/TL/KS/MUL/DT1/AMS-EN and the channel FL/CON map directly to YM2612 registers. LFO LFRQ is approximated onto the OPN2 `lfo_frequency` table (OPM 0–255 → OPN2 0–7); the two chips use different LFO clocks so absolute rates differ. OPM AMD / PMD have no OPN2 counterpart and are discarded, but they gate the per-channel AMS / PMS: if AMD=0 the OPN2 AMS is forced to 0 (and likewise PMS for PMD=0), because copying OPM's sensitivity without its depth register would introduce modulation that wasn't in the source patch. DT2, LFO WF, NE and the SLOT mask are discarded with warnings.

`.tfi` is the 42-byte fixed-size TFM Music Maker format, designed for the YM2203 (see [VGMrips wiki](https://vgmrips.net/wiki/TFI_File_Format)). FM core parameters map 1:1 onto YM2612 registers and round-trip losslessly. Because the YM2203 has no hardware LFO, TFI cannot store any LFO-related fields — LFO enable/frequency, channel AMS/FMS, and per-op AM-EN are silently dropped on serialize.

`.vgi` is the 43-byte format of Shiru's VGM Music Maker, also read by DefleMask and Furnace. It is a superset of TFI: the same operator layout plus channel FMS/AMS and the per-op AM-EN flag, so it preserves everything TFI does and more. Like TFI it stores detune in a linear 0-6 encoding, which collapses the hardware's +0/-0 distinction. Patch name, panning, and LFO enable/frequency are silently dropped on serialize.

`.eif` is the 29-byte FM instrument format of the [Echo sound engine](https://github.com/sikthehedgehog/Echo): a raw YM2612 register dump grouped by parameter. Because values are stored exactly as written to the chip, detune keeps its full hardware encoding (including +0 vs -0) and round-trips losslessly. Channel FMS/AMS is not part of the format (Echo drives `$B4` itself), and patch name, panning, and LFO are silently dropped on serialize.

`.vgm` / `.vgz` (gzip-compressed VGM is decompressed transparently) are register logs, not patch banks — instruments are reconstructed from the chip state, following the approach of [vgm2pre](https://github.com/vgmtool/vgm2pre) / Shiru's VGM2TFI. Every YM2612 write updates a shadow copy of the six channels' patch registers, and a key-on snapshots the channel: an instrument is "whatever the registers held when a note was struck". Silent states (all AR=0 or all TL=127) are ignored, and snapshots that differ only in carrier-operator TL — the driver's volume control — are grouped as one instrument, keeping the loudest variant. The global LFO register (`$22`) is captured at key-on; panning is performance data and is normalized to L=R=on. Inherent limitations: only instruments actually played appear, carrier TL reflects the loudest volume heard in the song, software envelopes/vibrato performed via register rewrites are not turned into macros, channel 6 is skipped while the DAC is enabled, and only the first chip of a dual-YM2612 VGM is scanned. Patches are named `<file>_<n>` in order of first appearance.

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

Files containing multiple patches (DMF, FUR, MML, GINPKG, VGM) are written as separate output files.

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
