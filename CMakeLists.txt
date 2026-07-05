cmake_minimum_required(VERSION 3.28) # FILE_SET CXX_MODULES
project(welder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WELDER_BUILD_MODULE "Build the importable `welder` C++20 module wrapper" ON)
option(WELDER_BUILD_PYBIND11 "Build the pybind11 Python backend" ON)
option(WELDER_BUILD_NANOBIND "Build the nanobind Python backend" ON)
option(WELDER_BUILD_SOL2 "Build the sol2 Lua backend" ON)
option(WELDER_BUILD_EXAMPLES "Build examples" ON)
option(WELDER_BUILD_TESTS "Build tests" ON)
option(WELDER_BUILD_STUBS "Generate Python .pyi stubs for built modules (pybind11 + nanobind)" ON)
option(WELDER_BUILD_DOCS "Build the documentation site (mkdocs + Doxygen reference)" OFF)

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

# Reusable CMake helpers (e.g. welder_pybind11_generate_stubs).
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(WelderPybind11Stubgen)
include(WelderSol2Module)
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
else()
  message(WARNING
    "welder: C++26 reflection flags are only wired for GNU (gcc-16). "
    "Compiler '${CMAKE_CXX_COMPILER_ID}' is not supported yet; the build will "
    "almost certainly fail until P2996/P3394 land there.")
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
