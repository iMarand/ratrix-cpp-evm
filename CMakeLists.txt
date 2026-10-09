cmake_minimum_required(VERSION 3.21)
project(Ratrix VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Vendored libsecp256k1 (third_party/secp256k1) for ECDSA transaction signing:
# the Ethereum recovery id and Bitcoin DER signatures come from the reference
# library rather than hand-rolled EC math. Only the recovery module is built.
set(SECP256K1_ENABLE_MODULE_RECOVERY ON  CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ECDH     OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_SCHNORRSIG OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_EXTRAKEYS  OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK        OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS      OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES         OFF CACHE BOOL "" FORCE)
set(SECP256K1_INSTALL                OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS                OFF)
add_subdirectory(third_party/secp256k1 EXCLUDE_FROM_ALL)

# Header-only crypto core + encrypted wallet store. Everything includes paths
# relative to the repo root (e.g. #include "core/rtx.h").
add_library(rtxcore INTERFACE)
target_include_directories(rtxcore INTERFACE ${CMAKE_SOURCE_DIR})
target_link_libraries(rtxcore INTERFACE OpenSSL::SSL OpenSSL::Crypto CURL::libcurl secp256k1)

# CLI (optional; also buildable via build/build-cli.sh)
option(RATRIX_BUILD_CLI "Build the command-line wallet" ON)
if(RATRIX_BUILD_CLI)
  add_executable(ratrix app/main.cpp)
  target_link_libraries(ratrix PRIVATE rtxcore)
endif()

# GUI
option(RATRIX_BUILD_GUI "Build the Qt6 desktop wallet" ON)
if(RATRIX_BUILD_GUI)
  add_subdirectory(gui)
endif()
