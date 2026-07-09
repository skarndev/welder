cmake_minimum_required(VERSION 3.28)
project(welder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# welder ships header-only for now — the optional C++20 module wrapper was removed
# until the gcc-16 -freflection/modules bugs are fixed and other toolchains catch
# up (see docs "Header-only for now").
option(WELDER_BUILD_PYBIND11 "Build the pybind11 Python backend" ON)
option(WELDER_BUILD_NANOBIND "Build the nanobind Python backend" ON)
option(WELDER_BUILD_SOL2 "Build the sol2 Lua backend" ON)
option(WELDER_BUILD_LUABRIDGE "Build the LuaBridge3 Lua backend" ON)
option(WELDER_BUILD_EXAMPLES "Build examples" ON)
option(WELDER_BUILD_TESTS "Build tests" ON)
option(WELDER_BUILD_STUBS "Generate Python .pyi stubs for built modules (pybind11 + nanobind)" ON)
option(WELDER_BUILD_DOCS "Build the documentation site (mkdocs + Doxygen reference)" OFF)

# Experimental: build with Bloomberg's clang-p2996 fork instead of gcc-16. Opt-in
# because the fork's reflection flag (-freflection-latest) is rejected by stock
# Clang, so the Clang branch of welder_flags must not fire for an ordinary clang.
# Tracked by .github/workflows/experimental-clang-p2996.yml.
option(WELDER_CLANG_P2996 "Enable the clang-p2996 fork reflection flags (experimental)" OFF)

# Build the nanobind extension against Python's stable ABI (abi3): links
# python3.dll (not pythonXY.dll) via Py_LIMITED_API, so one binary loads across
# Python minors AND across the compiler boundary — the C-ABI limited API is
# GCC/MSVC-compatible. This is what lets a UCRT-MinGW gcc build load in a stock
# MSVC CPython (e.g. Blender's). Requires nanobind + Python >= 3.12 + CMake's
# Development.SABIModule. (pybind11 has no equivalent — nanobind only.)
option(WELDER_NANOBIND_STABLE_ABI "Build the nanobind extension against Python's stable ABI (abi3)" OFF)

# Language-runtime version knobs. Conan supplies the C++ backend headers (sol2,
# pybind11, nanobind) at the versions we test with, but the *language runtimes*
# (Python, Lua) come from the system/user: an extension's ABI must match the
# interpreter that loads it, and only a real install carries an interpreter plus a
# package manager (uv, luarocks). These pin which minor the backends build against
# and which their test environments target; override to run against your own.
set(WELDER_LUA_VERSION "5.4" CACHE STRING
  "Lua minor the sol2 backend builds against and its tests run with")
set(WELDER_LUA_DIR "" CACHE PATH
  "Lua install prefix (e.g. `brew --prefix lua@5.4`) providing the headers, \
interpreter and luarocks target; empty = search the system")
set(WELDER_PYTHON_VERSION "3.14" CACHE STRING
  "Python minor the Python backends build against and their uv env targets")

# The LuaBridge3 rod builds against its OWN Lua (LuaBridge3 supports Lua 5.1–5.5 +
# LuaJIT/Luau, unlike sol2/3.5.0 which caps at 5.4), so it gets independent version
# knobs — but they default to the sol2 ones, so a single Lua install serves both.
# Point these at, e.g., `brew --prefix lua@5.5` to exercise the LuaBridge3 rod on a
# newer Lua than sol2 can use, without disturbing the sol2 build.
set(WELDER_LUABRIDGE_LUA_VERSION "${WELDER_LUA_VERSION}" CACHE STRING
  "Lua minor the LuaBridge3 backend builds against and its tests run with")
set(WELDER_LUABRIDGE_LUA_DIR "${WELDER_LUA_DIR}" CACHE PATH
  "Lua install prefix for the LuaBridge3 backend; empty = search the system")

# Reusable CMake helpers (e.g. welder_pybind11_generate_stubs).
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(WelderPybind11Stubgen)
include(WelderSol2Module)
include(WelderLuaBridgeModule)
include(WelderLuaCATSStub)

# All toolchain-specific flags for C++26 reflection + annotations live here, so
# the rest of the project stays standard. gcc-16 is currently the only compiler
# that implements P2996/P3394; add branches as others catch up.
add_library(welder_flags INTERFACE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR "welder needs GCC >= 16 for C++26 reflection (got ${CMAKE_CXX_COMPILER_VERSION})")
  endif()
  target_compile_options(welder_flags INTERFACE -freflection)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WELDER_CLANG_P2996)
  # Experimental: Bloomberg's clang-p2996 fork implements P2996 *and* P3394
  # (annotations) behind the single -freflection-latest flag, and ships its <meta>
  # in libc++ (the fork defaults to -stdlib=libc++). Gated on WELDER_CLANG_P2996
  # because stock Clang rejects the flag outright — this branch is only for the fork.
  target_compile_options(welder_flags INTERFACE -freflection-latest)
else()
  message(WARNING
    "welder: C++26 reflection flags are only wired for GNU (gcc-16) and, "
    "experimentally, Bloomberg's clang-p2996 fork (-DWELDER_CLANG_P2996=ON). "
    "Compiler '${CMAKE_CXX_COMPILER_ID}' is not supported here; the build will "
    "almost certainly fail until P2996/P3394 land there.")
endif()

# --- LuaBridge3 provisioning ---------------------------------------------------
# LuaBridge3 has no Conan Center package and ships no CMake config, so — per the
# project's provider-neutral stance — a *consumer* brings their own (find it via
# WELDER_LUABRIDGE_DIR, or a system install), while *our* build fetches a pinned
# copy for the tests/examples. Either way this resolves to LuaBridge3's own
# `LuaBridge` INTERFACE target, which the welder::luabridge rod links.
set(WELDER_LUABRIDGE_GIT_TAG "534a639d4857e64512e2b1ebcd7eb5dc8e728c08" CACHE STRING
  "LuaBridge3 git tag/commit to FetchContent when it is not found locally")
set(WELDER_LUABRIDGE_DIR "" CACHE PATH
  "LuaBridge3 source/install prefix (containing LuaBridge/LuaBridge.h); empty = search")
set(WELDER_LUABRIDGE_FETCH ${WELDER_BUILD_TESTS} CACHE BOOL
  "Fetch LuaBridge3 via FetchContent when it is not found locally")

if(WELDER_BUILD_LUABRIDGE AND NOT TARGET LuaBridge)
  # Consumer path: a user-provided or system LuaBridge3 (header-only). Try a CMake
  # package first (in case a future distribution ships one), then a header search.
  find_package(LuaBridge3 CONFIG QUIET)
  if(NOT TARGET LuaBridge)
    if(WELDER_LUABRIDGE_DIR)
      find_path(WELDER_LUABRIDGE_INCLUDE_DIR NAMES LuaBridge/LuaBridge.h
        PATHS "${WELDER_LUABRIDGE_DIR}" "${WELDER_LUABRIDGE_DIR}/Source" NO_DEFAULT_PATH)
    else()
      find_path(WELDER_LUABRIDGE_INCLUDE_DIR NAMES LuaBridge/LuaBridge.h)
    endif()
    if(WELDER_LUABRIDGE_INCLUDE_DIR)
      add_library(LuaBridge INTERFACE IMPORTED)
      target_include_directories(LuaBridge SYSTEM INTERFACE ${WELDER_LUABRIDGE_INCLUDE_DIR})
    endif()
  endif()
  # Testing/example convenience: fetch a pinned copy when nothing was provided.
  if(NOT TARGET LuaBridge AND WELDER_LUABRIDGE_FETCH)
    message(STATUS "welder: fetching LuaBridge3 (${WELDER_LUABRIDGE_GIT_TAG})...")
    include(FetchContent)
    FetchContent_Declare(LuaBridge3
      GIT_REPOSITORY https://github.com/kunitoki/LuaBridge3.git
      GIT_TAG ${WELDER_LUABRIDGE_GIT_TAG}
      GIT_SHALLOW FALSE)
    set(LUABRIDGE_TESTING OFF CACHE BOOL "" FORCE) # don't build LuaBridge3's own tests
    FetchContent_MakeAvailable(LuaBridge3)         # defines the `LuaBridge` target
  endif()
  if(NOT TARGET LuaBridge)
    message(WARNING "welder: LuaBridge3 not found; the LuaBridge3 backend "
      "(welder::luabridge) is disabled. Provide it with -DWELDER_LUABRIDGE_DIR=<dir> "
      "or allow fetching with -DWELDER_LUABRIDGE_FETCH=ON.")
    set(WELDER_BUILD_LUABRIDGE OFF)
  endif()
endif()

add_subdirectory(src)

if(WELDER_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(WELDER_BUILD_TESTS AND (WELDER_BUILD_PYBIND11 OR WELDER_BUILD_NANOBIND))
  enable_testing()
  add_subdirectory(tests)
endif()

# The documentation site. Off by default: it needs doxygen + a Python with mkdocs
# (provisioned via uv), none of which the library build itself requires. Wholly
# self-contained under docs/ and never touches the compiler flags above.
if(WELDER_BUILD_DOCS)
  add_subdirectory(docs)
endif()
