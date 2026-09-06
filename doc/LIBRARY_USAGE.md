# Library usage

C++, C, and Rust APIs for `ym2612_format`.

## C++

```cmake
add_subdirectory(ym2612_format)
target_link_libraries(your_target PRIVATE ym2612_format)
```

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

`all_formats()` returns a `FormatInfo` for every format; each entry's `aliases` field lists additional extensions beyond its primary `extension` (e.g. VGM's `extension` is `vgm` with `aliases` `["vgz"]`).

## C API

Header: `include/ym2612_format/c_api.h`. All returned memory is malloc'd; free it with `ym2612_free`. There is no global state.

- `ym2612_version()` — library version string.
- `ym2612_formats_json()` — JSON array of format descriptors.
- `ym2612_parse_json(...)` — parse instrument data, return JSON.
- `ym2612_convert(...)` — parse, then serialize one patch to a target format, return raw bytes.
- `ym2612_free(ptr)` — free memory returned by any of the above.

```c
#include <ym2612_format/c_api.h>

char *error = NULL;
char *json = ym2612_parse_json(data, size, "input.dmp", NULL, &error);
if (!json) {
  fprintf(stderr, "%s\n", error);
  ym2612_free(error);
}
ym2612_free(json);

size_t out_size = 0;
uint8_t *tfi = ym2612_convert(data, size, "input.dmp", NULL, 0, "tfi",
                              &out_size, &error);
ym2612_free(tfi);
```

`ym2612_formats_json` returns descriptors like:

```json
[{"format":"vgm","name":"VGM/VGZ register log","extension":"vgm",
  "extensions":["vgm","vgz"],"can_read":true,"can_write":false,"is_text":false}]
```

`ym2612_parse_json` returns, on success:

```json
{"patches":[{"name":"...","algorithm":0,"feedback":0,
             "has_macros":false,"mml":"@1 fm ...\n..."}],
 "warnings":["..."]}
```

On failure it returns `NULL` and writes an error message to `*error` (free with `ym2612_free`).

## Rust

Crate `rust/ym2612-format` in this repository, used as a git dependency:

```toml
[dependencies]
ym2612-format = { git = "https://github.com/ulalume/ym2612_format.git" }
```

Pin a release with `tag = "vX.Y.Z"` when reproducible builds matter.

```rust
let data = std::fs::read("bright piano.dmp").unwrap();

// Library version
let version = ym2612_format::version();

// Every supported format
let formats = ym2612_format::formats();

// Parse instrument data (format hint taken from the name's extension, or pass Some("dmp"))
let parsed = ym2612_format::parse(&data, "bright piano.dmp", None).unwrap();

// Parse, then serialize patch 0 to TFI
let tfi = ym2612_format::convert(&data, "bright piano.dmp", None, 0, "tfi").unwrap();
```

The build script compiles the C++ sources with `cc`; a C++20 compiler is required. The crate is not published on crates.io.
