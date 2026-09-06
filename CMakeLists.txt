cmake_minimum_required(VERSION 3.20)
project(ym2612_format VERSION 0.2.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options ---
option(YM2612_FORMAT_BUILD_CLI "Build the CLI tool" ON)
option(YM2612_FORMAT_BUILD_TESTS "Build tests" OFF)

# --- Dependencies ---
include(FetchContent)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# miniz 3.1.1 amalgamation, vendored in third_party/miniz.
enable_language(C)
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz>
  $<INSTALL_INTERFACE:include>
)

# nlohmann/json is only needed by the CLI tool (info --json output).
# The library itself uses a lightweight internal JSON implementation.
if(YM2612_FORMAT_BUILD_CLI)
  FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  )
  FetchContent_MakeAvailable(json)
endif()

# --- Library ---
add_library(ym2612_format STATIC
  src/c_api.cpp
  src/dmf.cpp
  src/dmp.cpp
  src/fui.cpp
  src/fur.cpp
  src/gin.cpp
  src/ginpkg.cpp
  src/rym2612.cpp
  src/ctrmml.cpp
  src/opm.cpp
  src/tfi.cpp
  src/vgi.cpp
  src/eif.cpp
  src/vgm.cpp
  src/converter.cpp
)

target_include_directories(ym2612_format PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_include_directories(ym2612_format PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(ym2612_format PRIVATE
  YM2612_FORMAT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(ym2612_format PUBLIC miniz)

# --- CLI ---
if(YM2612_FORMAT_BUILD_CLI)
  add_executable(ym2612_convert src/cli/main.cpp)
  target_link_libraries(ym2612_convert PRIVATE ym2612_format nlohmann_json::nlohmann_json)
endif()

# --- Tests ---
if(YM2612_FORMAT_BUILD_TESTS)
  enable_testing()

  add_executable(ym2612_format_test test/roundtrip_test.cpp)
  target_link_libraries(ym2612_format_test PRIVATE ym2612_format)
  target_compile_definitions(ym2612_format_test PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test"
    SAMPLE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sample"
  )

  add_test(NAME roundtrip_test COMMAND ym2612_format_test)
endif()
