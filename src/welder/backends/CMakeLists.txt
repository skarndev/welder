# Header-only language backends. Each backend is a thin adapter consuming the
# same core resolution API (welded_for / member_bound / public_bases) plus its
# own target framework. Each backend struct keeps the welder::backend contract
# public and its framework-specific helpers protected (prefixed _); cross-backend
# maps are factored into shared headers next to the backends (Python: doc_style.hpp
# + operators.hpp; sol2: metamethods.hpp; luacats: type_map.hpp + document.hpp).

# --- welder::pybind11 : pybind11 Python backend ------------------------------
if(WELDER_BUILD_PYBIND11)
  find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(pybind11 REQUIRED)

  add_library(welder_pybind11 INTERFACE)
  add_library(welder::pybind11 ALIAS welder_pybind11)
  target_link_libraries(welder_pybind11 INTERFACE
    welder_headers pybind11::headers Python::Module)
endif()

# --- welder::nanobind : nanobind Python backend ------------------------------
# nanobind ships its runtime as source compiled *into* each extension via its own
# `nanobind_add_module()` (from nanobind-config.cmake), not as a plain link
# library — so this INTERFACE target only surfaces the welder + nanobind headers;
# an extension must still be created with nanobind_add_module (which adds the
# runtime, visibility and link flags). nanobind's config requires Python to be
# found first and defines NB_DIR (its package root).
if(WELDER_BUILD_NANOBIND)
  find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(nanobind CONFIG REQUIRED)

  add_library(welder_nanobind INTERFACE)
  add_library(welder::nanobind ALIAS welder_nanobind)
  target_link_libraries(welder_nanobind INTERFACE welder_headers)
  target_include_directories(welder_nanobind SYSTEM INTERFACE ${NB_DIR}/include)
endif()

# --- welder::sol2 : sol2 Lua backend -----------------------------------------
# A loadable Lua C module is a shared object that Lua's `require` finds on
# `package.cpath` and enters through `luaopen_<name>`; it resolves the `lua_*`
# symbols from the *host* interpreter at load time and must NOT bundle its own
# Lua runtime (a second copy would mismatch the host's `lua_State`). So this
# INTERFACE target surfaces only the sol2 + Lua *headers* — it deliberately does
# not link a Lua library (which carries `liblua`). Create the extension with
# welder_sol2_add_module() (see cmake/WelderSol2Module.cmake), which sets the
# bare `<name>.so` output and the host-symbol link model.
#
# sol2 (the C++ wrapper) comes from conan, but Lua itself comes from the *system /
# user* via CMake's builtin FindLua — not conan's transitive `lua`. The module's
# ABI must match the interpreter that will `require` it (Lua has no cross-minor
# ABI), and only a real install carries that interpreter + luarocks, so the
# runtime Lua is the single source of truth for the headers too. Steer it with
# -DWELDER_LUA_DIR (an install prefix) and pin the minor with -DWELDER_LUA_VERSION.
if(WELDER_BUILD_SOL2)
  find_package(sol2 REQUIRED)
  # Pin the Lua headers to the user's install. conan puts its transitive lua on
  # CMAKE_PREFIX_PATH, which otherwise shadows FindLua's own search (and even
  # `Lua_ROOT`), so when WELDER_LUA_DIR is given we resolve LUA_INCLUDE_DIR under it
  # explicitly and hand the result to FindLua (which honors a preset value and
  # reads the version from it). Cleared first so a reconfigure re-pins.
  if(WELDER_LUA_DIR)
    unset(LUA_INCLUDE_DIR CACHE)
    find_path(LUA_INCLUDE_DIR lua.h
      PATHS "${WELDER_LUA_DIR}/include"
      PATH_SUFFIXES "lua${WELDER_LUA_VERSION}" lua ""
      NO_DEFAULT_PATH)
  endif()
  find_package(Lua MODULE REQUIRED)   # builtin FindLua: system/user, not conan
  if(NOT "${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}" STREQUAL WELDER_LUA_VERSION)
    message(FATAL_ERROR
      "welder: found Lua ${LUA_VERSION_STRING} but WELDER_LUA_VERSION is "
      "${WELDER_LUA_VERSION}. A sol2 module built against one Lua minor segfaults "
      "in another. Point -DWELDER_LUA_DIR at a matching install "
      "(e.g. `brew --prefix lua@${WELDER_LUA_VERSION}`) or set -DWELDER_LUA_VERSION.")
  endif()

  add_library(welder_sol2 INTERFACE)
  add_library(welder::sol2 ALIAS welder_sol2)
  target_link_libraries(welder_sol2 INTERFACE welder_headers)
  # Headers only (see note above): sol2's own include dir plus the user's Lua
  # headers, without linking either library.
  target_include_directories(welder_sol2 SYSTEM INTERFACE
    $<TARGET_PROPERTY:sol2::sol2,INTERFACE_INCLUDE_DIRECTORIES>
    ${LUA_INCLUDE_DIR})
  # Match the probes: enable sol2's strong safety set (type/bounds checks).
  target_compile_definitions(welder_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)
endif()

# --- welder::luacats : LuaCATS `---@meta` stub backend -----------------------
# The Lua counterpart of the Python `.pyi` stubs: a text-emitting backend that
# reflects the same welded types (via the same generic driver as sol2) and writes
# a LuaCATS definition file carrying the docstrings Lua drops at runtime. It is
# pure reflection + text — it does NOT depend on sol2 or a Lua runtime — so this
# is an unconditional INTERFACE target over the header-only core (which already
# carries the -freflection flag via welder_flags). Build a stub with
# welder_luacats_generate_stub() (see cmake/WelderLuaCATSStub.cmake).
add_library(welder_luacats INTERFACE)
add_library(welder::luacats ALIAS welder_luacats)
target_link_libraries(welder_luacats INTERFACE welder_headers)
