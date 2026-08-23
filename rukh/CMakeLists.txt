cmake_minimum_required(VERSION 3.28)
project(rukh)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(RUKH_DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH
    "Persistent dependency cache")

set(RUKH_DEPS_INSTALL_DIR "${RUKH_DEPS_DIR}/install" CACHE PATH
    "Persistent dependency install directory")

set(FETCHCONTENT_BASE_DIR "${RUKH_DEPS_DIR}/fetchcontent" CACHE PATH
    "FetchContent cache directory")

include(ExternalProject)
include(FetchContent)

# ── Sources ───────────────────────────────────────────────────────────────────
file(GLOB_RECURSE RUKH_SOURCES "src/*.cpp")
add_library(rukh STATIC ${RUKH_SOURCES})

# ── Compile options ───────────────────────────────────────────────────────────
target_compile_definitions(rukh PRIVATE
  $<IF:$<CONFIG:Debug>,SPDLOG_ACTIVE_LEVEL=0,SPDLOG_ACTIVE_LEVEL=2>
)
target_compile_options(rukh PRIVATE
  $<IF:$<CONFIG:Release>,-O3,-O0 -g>
)

# ── Include paths ─────────────────────────────────────────────────────────────
# PUBLIC  → consumers get <rukh/HttpRequest> etc.
# PRIVATE → src/*.cpp can write #include "HttpRequest.hpp"
#            or #include "core/Executor.hpp"
target_include_directories(rukh
  PUBLIC  include/
)

# ── SQLite ────────────────────────────────────────────────────────────────────
option(RUKH_ENABLE_SQLITE "Enable SQLite database backend" ON)

if(RUKH_ENABLE_SQLITE)
  FetchContent_Declare(
    sqlite3
    URL https://sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH SHA3_256=628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
  )

  FetchContent_MakeAvailable(sqlite3)

  add_library(sqlite3_amalgamation OBJECT
    ${sqlite3_SOURCE_DIR}/sqlite3.c
  )

  target_include_directories(sqlite3_amalgamation PUBLIC
    ${sqlite3_SOURCE_DIR}
  )

  target_compile_definitions(sqlite3_amalgamation PUBLIC
    SQLITE_THREADSAFE=2
  )

  target_compile_options(sqlite3_amalgamation PRIVATE
    -O2
  )

  target_compile_definitions(rukh PRIVATE
    RUKH_ENABLE_SQLITE
  )

  target_link_libraries(rukh PUBLIC
    sqlite3_amalgamation
  )
endif()

# ── liburing (ExternalProject) ────────────────────────────────────────────────
ExternalProject_Add(liburing_ext
  GIT_REPOSITORY https://github.com/axboe/liburing.git
  GIT_TAG liburing-2.15
  PREFIX ${RUKH_DEPS_DIR}/liburing

  CONFIGURE_COMMAND
    <SOURCE_DIR>/configure --prefix=${RUKH_DEPS_INSTALL_DIR}

  BUILD_COMMAND make -j
  INSTALL_COMMAND make install
  BUILD_IN_SOURCE 1

  BUILD_BYPRODUCTS ${RUKH_DEPS_INSTALL_DIR}/lib/liburing.a
)

file(MAKE_DIRECTORY ${RUKH_DEPS_INSTALL_DIR}/include)

add_library(liburing_imported STATIC IMPORTED)

set_target_properties(liburing_imported PROPERTIES
  IMPORTED_LOCATION ${RUKH_DEPS_INSTALL_DIR}/lib/liburing.a
  INTERFACE_INCLUDE_DIRECTORIES ${RUKH_DEPS_INSTALL_DIR}/include
)

add_dependencies(rukh liburing_ext)

# ── System dependencies ───────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)
find_package(ZLIB    REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json REQUIRED)

pkg_check_modules(BROTLIENC    REQUIRED IMPORTED_TARGET libbrotlienc)
pkg_check_modules(BROTLICOMMON REQUIRED IMPORTED_TARGET libbrotlicommon)

# ── Link ──────────────────────────────────────────────────────────────────────
# PUBLIC because consumers transitively need these symbols
# (e.g. OpenSSL types appear in TlsStream.hpp which is in core/)
target_link_libraries(rukh PUBLIC
  OpenSSL::SSL
  OpenSSL::Crypto
  ZLIB::ZLIB
  liburing_imported
  PkgConfig::BROTLIENC
  PkgConfig::BROTLICOMMON
  nlohmann_json::nlohmann_json
)

# ── PCH ───────────────────────────────────────────────────────────────────────
target_precompile_headers(rukh PRIVATE
  # third-party
  <openssl/ssl.h>
  <openssl/err.h>
  <brotli/encode.h>
  <liburing.h>

  # stdlib
  <string>
  <string_view>
  <vector>
  <unordered_map>
  <unordered_set>
  <map>
  <set>
  <array>
  <deque>
  <span>
  <optional>
  <variant>
  <expected>
  <memory>
  <functional>
  <algorithm>
  <numeric>
  <chrono>
  <thread>
  <mutex>
  <shared_mutex>
  <atomic>
  <filesystem>
  <format>
  <ranges>
  <source_location>
  <cstdint>
  <cstddef>
  <cassert>
  <exception>
  <stdexcept>
)

# ── Alias ─────────────────────────────────────────────────────────────────────
add_library(rukh::rukh ALIAS rukh)
