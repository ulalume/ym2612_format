cmake_minimum_required(VERSION 3.20)
project(ym2612_format VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options ---
option(YM2612_FORMAT_BUILD_CLI "Build the CLI tool" ON)
option(YM2612_FORMAT_BUILD_TESTS "Build tests" OFF)

# --- Dependencies ---
include(FetchContent)

FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

FetchContent_Declare(
  miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG 3.1.1
)

FetchContent_MakeAvailable(json miniz)

# --- Library ---
add_library(ym2612_format STATIC
  src/dmp.cpp
  src/fui.cpp
  src/gin.cpp
  src/ginpkg.cpp
  src/rym2612.cpp
  src/ctrmml.cpp
  src/converter.cpp
)

target_include_directories(ym2612_format PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(ym2612_format PUBLIC nlohmann_json::nlohmann_json miniz)

# --- CLI ---
if(YM2612_FORMAT_BUILD_CLI)
  add_executable(ym2612_convert src/cli/main.cpp)
  target_link_libraries(ym2612_convert PRIVATE ym2612_format)
endif()
