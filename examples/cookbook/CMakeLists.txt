# The welder cookbook — a set of small, self-contained consumer projects, one per
# recipe, each demonstrating a slice of welder's surface end to end (annotate →
# weld → load → assert). Documented page-by-page in docs/content/cookbook/.
#
# Unlike examples/{python_poc,lua_poc,...} (which build inside welder's own tree),
# the cookbook is a STANDALONE super-project that obtains welder the way a real
# consumer does — FetchContent — so building it also exercises welder's packaging
# seams (the subproject collapse to `welder::headers`, the opt-in backend targets,
# the CMake helpers). It is therefore NOT add_subdirectory()'d from welder's build;
# configure it on its own:
#
#   cmake -S examples/cookbook -B build/cookbook -G Ninja \
#     -DCMAKE_CXX_COMPILER=g++-16 \
#     -DFETCHCONTENT_SOURCE_DIR_WELDER=$PWD \          # CI/local: use this checkout
#     -DWELDER_LUA_DIR="$(brew --prefix lua@5.4)" \    # for the Lua recipe
#     -DPython_EXECUTABLE=<python3.14>                 # if not first on PATH
#   cmake --build build/cookbook
#   ctest --test-dir build/cookbook --output-on-failure
#
# Without FETCHCONTENT_SOURCE_DIR_WELDER, welder is fetched from GitHub — which is
# exactly what a reader copying a recipe into their own project would get.
cmake_minimum_required(VERSION 3.28)
project(welder-cookbook LANGUAGES CXX)

# welder checks (never imposes) the standard + reflection flag: the consumer sets
# both. gcc-16 is the only P2996 compiler today, so the flag is unconditional here.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-freflection)

include(FetchContent)

# --- the language backends ------------------------------------------------------
# welder is bring-your-own-backend: enabling WELDER_BUILD_<rod> makes welder wire
# its INTERFACE rod target (welder::pybind11, ...) around a backend *you* provide.
# Here each backend is FetchContent-pinned (the versions welder tests against) and
# OVERRIDE_FIND_PACKAGE redirects welder's own find_package(<backend>) calls to
# these copies — no Conan, no preinstalled packages.
FetchContent_Declare(pybind11
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG v3.0.1
  OVERRIDE_FIND_PACKAGE)
FetchContent_Declare(nanobind
  GIT_REPOSITORY https://github.com/wjakob/nanobind.git
  GIT_TAG v2.13.0
  OVERRIDE_FIND_PACKAGE)
FetchContent_Declare(sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2.git
  GIT_TAG v3.5.0
  OVERRIDE_FIND_PACKAGE)

# One Python for everything: the recipes' extension modules, nanobind's stubgen and
# the check scripts. Found before welder so both resolve the same interpreter.
# (WELDER_PYTHON_VERSION is welder's minimum-minor knob; sharing it keeps the two
# find_package(Python) calls — ours and welder's — consistent.)
set(WELDER_PYTHON_VERSION "3.14" CACHE STRING
  "Python minor the cookbook (and welder's backends) build against")
find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED
  COMPONENTS Interpreter Development.Module)

# --- welder itself ----------------------------------------------------------------
# Pulled in as a subproject, welder builds nothing of its own; each WELDER_BUILD_*
# flipped ON opts into that rod's INTERFACE target + helper wiring.
set(WELDER_BUILD_PYBIND11 ON)
set(WELDER_BUILD_NANOBIND ON)

# The Lua rods need a real Lua install to match the interpreter that will
# `require` the modules (a cross-minor mismatch segfaults at require). sol2 rides
# WELDER_LUA_DIR (Lua <= 5.4); LuaBridge3 has its own knob defaulting to the sol2
# one (WELDER_LUABRIDGE_LUA_DIR — point it at a 5.5 install where sol2 can't
# follow, e.g. ucrt64). Each defaults ON when its Lua is available; pass an
# explicit -DWELDER_BUILD_<rod>=OFF to override (CI turns sol2 off on Windows).
if(NOT DEFINED WELDER_BUILD_SOL2)
  if(WELDER_LUA_DIR)
    set(WELDER_BUILD_SOL2 ON)
  else()
    message(STATUS "cookbook: WELDER_LUA_DIR not set - skipping the sol2 side of "
      "07-multilang (pass -DWELDER_LUA_DIR=<lua install prefix> to enable it)")
    set(WELDER_BUILD_SOL2 OFF)
  endif()
endif()
if(NOT DEFINED WELDER_BUILD_LUABRIDGE)
  if(WELDER_LUABRIDGE_LUA_DIR OR WELDER_LUA_DIR)
    set(WELDER_BUILD_LUABRIDGE ON)
  else()
    message(STATUS "cookbook: no Lua install given - skipping the LuaBridge3 side "
      "of 07-multilang (pass -DWELDER_LUA_DIR or -DWELDER_LUABRIDGE_LUA_DIR)")
    set(WELDER_BUILD_LUABRIDGE OFF)
  endif()
endif()
# LuaBridge3 itself is header-only with no package to find — let welder's build
# FetchContent its pinned copy when the rod is enabled.
set(WELDER_LUABRIDGE_FETCH ${WELDER_BUILD_LUABRIDGE})

# Make the backends available at THIS scope before welder is pulled in, so their
# targets/config variables (e.g. nanobind's NB_DIR) are visible to welder's
# subdirectory when its find_package() calls are redirected here.
FetchContent_MakeAvailable(pybind11 nanobind)
if(WELDER_BUILD_SOL2)
  FetchContent_MakeAvailable(sol2)
endif()

FetchContent_Declare(welder
  GIT_REPOSITORY https://github.com/skarndev/welder.git
  GIT_TAG main)
FetchContent_MakeAvailable(welder)

# The Lua interpreters that run the Lua checks — each from the same install its
# module is built against.
if(WELDER_BUILD_SOL2)
  find_program(WELDER_COOKBOOK_LUA NAMES lua "lua${WELDER_LUA_VERSION}"
    HINTS "${WELDER_LUA_DIR}/bin" NO_DEFAULT_PATH)
  if(NOT WELDER_COOKBOOK_LUA)
    find_program(WELDER_COOKBOOK_LUA NAMES lua "lua${WELDER_LUA_VERSION}")
  endif()
endif()
if(WELDER_BUILD_LUABRIDGE)
  find_program(WELDER_COOKBOOK_LUABRIDGE_LUA
    NAMES lua "lua${WELDER_LUABRIDGE_LUA_VERSION}"
    HINTS "${WELDER_LUABRIDGE_LUA_DIR}/bin" "${WELDER_LUA_DIR}/bin" NO_DEFAULT_PATH)
  if(NOT WELDER_COOKBOOK_LUABRIDGE_LUA)
    find_program(WELDER_COOKBOOK_LUABRIDGE_LUA
      NAMES lua "lua${WELDER_LUABRIDGE_LUA_VERSION}")
  endif()
endif()

# pybind11 extension boilerplate shared by the pybind11 recipes. CMake-native
# Python_add_library + welder::pybind11 (includes pybind11::headers); hidden
# visibility matches pybind11's own type model (pybind11_add_module would set it,
# the native path doesn't). Each recipe stays a plain, copyable CMakeLists — this
# helper only removes the repetition across them.
function(cookbook_add_pybind11_module target)
  Python_add_library(${target} MODULE WITH_SOABI ${ARGN})
  target_link_libraries(${target} PRIVATE welder::headers welder::pybind11)
  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endfunction()

# Each recipe registers a `cookbook.<name>` CTest that imports/loads the built
# module(s) and asserts the bound surface.
enable_testing()

add_subdirectory(01-hello)
add_subdirectory(02-discovery)
add_subdirectory(03-inheritance)
add_subdirectory(04-virtuals)
add_subdirectory(05-generated-trampolines)
add_subdirectory(06-templates)
add_subdirectory(07-multilang)
add_subdirectory(08-tack-welding)
add_subdirectory(09-custom-traversal)