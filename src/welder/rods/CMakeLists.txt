# Header-only language rods (backends). Each rod is a thin adapter consuming the
# same core resolution API (welded_for / member_bound / public_bases) plus its
# own target framework. Each rod struct (welder::rods::<name>::rod) keeps the
# welder::rod contract public and its framework-specific helpers protected
# (prefixed _); cross-rod maps are factored into shared headers next to the rods
# (Python: doc_style.hpp + operators.hpp; sol2: metamethods.hpp; luacats:
# type_map.hpp + document.hpp).

# --- welder::pybind11 : pybind11 Python rod ----------------------------------
if(WELDER_BUILD_PYBIND11)
  find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(pybind11 REQUIRED)

  add_library(welder_pybind11 INTERFACE)
  add_library(welder::pybind11 ALIAS welder_pybind11)
  target_link_libraries(welder_pybind11 INTERFACE
    welder_headers pybind11::headers Python::Module)
  # Consume pybind11's headers as SYSTEM (as we do for nanobind/sol2/LuaBridge3), so
  # warnings from pybind11-internal code inlined into a binding TU (e.g. a spurious
  # -Wnull-dereference in its dispatcher's std::copy) don't surface under welder's
  # strict warning set. find_package(pybind11) leaves them non-SYSTEM. When pybind11
  # arrives via FetchContent/add_subdirectory (e.g. the cookbook) pybind11::headers
  # is an ALIAS, which set_target_properties rejects — resolve to the real target.
  get_target_property(_welder_pb_tgt pybind11::headers ALIASED_TARGET)
  if(NOT _welder_pb_tgt)
    set(_welder_pb_tgt pybind11::headers)
  endif()
  get_target_property(_welder_pb_inc ${_welder_pb_tgt} INTERFACE_INCLUDE_DIRECTORIES)
  if(_welder_pb_inc)
    set_target_properties(${_welder_pb_tgt} PROPERTIES
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_welder_pb_inc}")
  endif()
endif()

# --- welder::nanobind : nanobind Python rod ------------------------------
# nanobind ships its runtime as source compiled *into* each extension via its own
# `nanobind_add_module()` (from nanobind-config.cmake), not as a plain link
# library — so this INTERFACE target only surfaces the welder + nanobind headers;
# an extension must still be created with nanobind_add_module (which adds the
# runtime, visibility and link flags). nanobind's config requires Python to be
# found first and defines NB_DIR (its package root).
if(WELDER_BUILD_NANOBIND)
  # Development.SABIModule (CMake >= 3.26) is what lets nanobind's STABLE_ABI engage;
  # without it STABLE_ABI silently downgrades to a version-locked module.
  set(_welder_nb_py_components Interpreter Development.Module)
  if(WELDER_NANOBIND_STABLE_ABI)
    list(APPEND _welder_nb_py_components Development.SABIModule)
  endif()
  find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED COMPONENTS ${_welder_nb_py_components})
  find_package(nanobind CONFIG REQUIRED)

  add_library(welder_nanobind INTERFACE)
  add_library(welder::nanobind ALIAS welder_nanobind)
  target_link_libraries(welder_nanobind INTERFACE welder_headers)
  target_include_directories(welder_nanobind SYSTEM INTERFACE ${NB_DIR}/include)
endif()

# --- welder::sol2 : sol2 Lua rod -----------------------------------------
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
    # Pin the library from the same prefix too: FindLua hard-fails without
    # LUA_LIBRARIES, and a keg-only install (brew lua@5.4) is invisible to its
    # default search — welder's own conan build never noticed (conan's transitive
    # lua on CMAKE_PREFIX_PATH supplied a library), but a conan-less consumer
    # (the cookbook) does. welder never LINKS it (host-symbol model); pinning
    # just makes WELDER_LUA_DIR self-sufficient and keeps the found version
    # consistent with the pinned headers.
    string(REPLACE "." "" _welder_lua_nodot "${WELDER_LUA_VERSION}")
    unset(LUA_LIBRARY CACHE)
    find_library(LUA_LIBRARY
      NAMES lua "lua${WELDER_LUA_VERSION}" "lua-${WELDER_LUA_VERSION}"
            "lua${_welder_lua_nodot}"
      PATHS "${WELDER_LUA_DIR}/lib" NO_DEFAULT_PATH)
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

# --- welder::luabridge : LuaBridge3 Lua rod ------------------------------
# A second Lua runtime rod alongside sol2, over LuaBridge3 instead of sol2. Same
# loadable-module story: a shared object Lua's `require` finds on `package.cpath`
# and enters via `luaopen_<name>`, resolving `lua_*` from the *host* interpreter —
# so this INTERFACE target surfaces only the LuaBridge3 + Lua *headers* and does not
# link a Lua library. Create an extension with welder_luabridge_add_module().
#
# LuaBridge3 (the header-only C++ wrapper) is provisioned by the top-level
# CMakeLists (find/dir for consumers, FetchContent for our build) as the `LuaBridge`
# target. Lua itself comes from the *system/user* via its own knobs
# (WELDER_LUABRIDGE_LUA_DIR / WELDER_LUABRIDGE_LUA_VERSION, defaulting to the sol2
# ones) — LuaBridge3 supports newer Lua minors than sol2, so it can point at a
# different install without disturbing welder::sol2.
if(WELDER_BUILD_LUABRIDGE AND TARGET LuaBridge)
  # Resolve the LuaBridge3 Lua headers independently of sol2's LUA_INCLUDE_DIR (the
  # two rods may target different Lua minors), into a private cache var. Header-only:
  # like sol2 it never links liblua (host-symbol model).
  if(WELDER_LUABRIDGE_LUA_DIR)
    find_path(WELDER_LUABRIDGE_LUA_INCLUDE_DIR lua.h
      PATHS "${WELDER_LUABRIDGE_LUA_DIR}/include"
      PATH_SUFFIXES "lua${WELDER_LUABRIDGE_LUA_VERSION}" lua ""
      NO_DEFAULT_PATH)
  else()
    find_path(WELDER_LUABRIDGE_LUA_INCLUDE_DIR lua.h
      PATH_SUFFIXES "lua${WELDER_LUABRIDGE_LUA_VERSION}" lua "")
  endif()
  if(NOT WELDER_LUABRIDGE_LUA_INCLUDE_DIR)
    message(FATAL_ERROR "welder: could not find lua.h for the LuaBridge3 backend. "
      "Set -DWELDER_LUABRIDGE_LUA_DIR to a Lua install prefix "
      "(e.g. `brew --prefix lua@${WELDER_LUABRIDGE_LUA_VERSION}`).")
  endif()
  # Guard the found Lua minor against the requested one: LuaBridge3 has no cross-minor
  # ABI, so a mismatch would segfault the interpreter that loads the module (same
  # hazard as sol2). Lua 5.5 carries the version as integer macros
  # (LUA_VERSION_MAJOR_N 5 / LUA_VERSION_MINOR_N 5); 5.4 and earlier as strings
  # (LUA_VERSION_MAJOR "5"). (LUA_VERSION_NUM is unusable — a literal in 5.4 but a
  # computed expression in 5.5.) Try the integer form, then fall back to the string.
  file(STRINGS "${WELDER_LUABRIDGE_LUA_INCLUDE_DIR}/lua.h" _wlb_major
    REGEX "^#define[ \t]+LUA_VERSION_MAJOR_N[ \t]+[0-9]+")
  file(STRINGS "${WELDER_LUABRIDGE_LUA_INCLUDE_DIR}/lua.h" _wlb_minor
    REGEX "^#define[ \t]+LUA_VERSION_MINOR_N[ \t]+[0-9]+")
  if(_wlb_major AND _wlb_minor)
    string(REGEX REPLACE ".*LUA_VERSION_MAJOR_N[ \t]+([0-9]+).*" "\\1" _wlb_major "${_wlb_major}")
    string(REGEX REPLACE ".*LUA_VERSION_MINOR_N[ \t]+([0-9]+).*" "\\1" _wlb_minor "${_wlb_minor}")
  else()
    file(STRINGS "${WELDER_LUABRIDGE_LUA_INCLUDE_DIR}/lua.h" _wlb_major
      REGEX "^#define[ \t]+LUA_VERSION_MAJOR[ \t]+\"[0-9]+\"")
    file(STRINGS "${WELDER_LUABRIDGE_LUA_INCLUDE_DIR}/lua.h" _wlb_minor
      REGEX "^#define[ \t]+LUA_VERSION_MINOR[ \t]+\"[0-9]+\"")
    string(REGEX REPLACE ".*\"([0-9]+)\".*" "\\1" _wlb_major "${_wlb_major}")
    string(REGEX REPLACE ".*\"([0-9]+)\".*" "\\1" _wlb_minor "${_wlb_minor}")
  endif()
  if(NOT "${_wlb_major}.${_wlb_minor}" STREQUAL WELDER_LUABRIDGE_LUA_VERSION)
    message(FATAL_ERROR
      "welder: found Lua ${_wlb_major}.${_wlb_minor} for the LuaBridge3 backend but "
      "WELDER_LUABRIDGE_LUA_VERSION is ${WELDER_LUABRIDGE_LUA_VERSION}. A module built "
      "against one Lua minor segfaults in another; point -DWELDER_LUABRIDGE_LUA_DIR at "
      "a matching install or set -DWELDER_LUABRIDGE_LUA_VERSION.")
  endif()

  add_library(welder_luabridge INTERFACE)
  add_library(welder::luabridge ALIAS welder_luabridge)
  target_link_libraries(welder_luabridge INTERFACE welder_headers LuaBridge)
  target_include_directories(welder_luabridge SYSTEM INTERFACE
    ${WELDER_LUABRIDGE_LUA_INCLUDE_DIR})

  # Windows PE has no ELF/Mach-O host-symbol lookup: a loadable module can't leave
  # `lua_*` unresolved for the interpreter to satisfy at load, so it must link the
  # Lua import library. The ucrt64 interpreter is linked against the same lua DLL,
  # so the module and host share one lua_State (the host-symbol model's intent).
  # (Header-only elsewhere — the ELF/macho rods never link liblua; see the note
  # above and the module helpers' APPLE branch.)
  if(WIN32)
    find_library(WELDER_LUABRIDGE_LUA_LIBRARY
      NAMES lua lua5.5 lua55 lua5.4 lua54 liblua
      PATHS "${WELDER_LUABRIDGE_LUA_DIR}/lib" "${WELDER_LUABRIDGE_LUA_DIR}/bin"
      NO_DEFAULT_PATH)
    if(NOT WELDER_LUABRIDGE_LUA_LIBRARY)
      find_library(WELDER_LUABRIDGE_LUA_LIBRARY NAMES lua lua5.5 lua55 lua5.4 lua54 liblua)
    endif()
    if(NOT WELDER_LUABRIDGE_LUA_LIBRARY)
      message(FATAL_ERROR "welder: could not find the Lua import library for the "
        "LuaBridge3 backend on Windows (needed to resolve lua_* in a loadable "
        "module). Set -DWELDER_LUABRIDGE_LUA_DIR to a Lua install prefix.")
    endif()
    target_link_libraries(welder_luabridge INTERFACE ${WELDER_LUABRIDGE_LUA_LIBRARY})
  endif()
endif()

# --- welder::luacats : LuaCATS `---@meta` stub rod -----------------------
# The Lua counterpart of the Python `.pyi` stubs: a text-emitting rod that
# reflects the same welded types (via the same generic driver as sol2) and writes
# a LuaCATS definition file carrying the docstrings Lua drops at runtime. It is
# pure reflection + text — it does NOT depend on sol2 or a Lua runtime — so this
# is an unconditional INTERFACE target over the header-only core (which already
# carries the -freflection flag via welder_flags). Build a stub with
# welder_luacats_generate_stub() (see cmake/WelderLuaCATSStub.cmake).
add_library(welder_luacats INTERFACE)
add_library(welder::luacats ALIAS welder_luacats)
target_link_libraries(welder_luacats INTERFACE welder_headers)

# --- welder::trampolines : Python trampoline-generator rod ---------------
# The Python counterpart of the LuaCATS stub rod: a text-emitting rod that reflects
# the welded virtual types (via the same generic driver as pybind11/nanobind) and
# writes a .hpp of ready-to-compile, backend-neutral trampoline subclasses, so a
# Python subclass can override their virtuals without the trampolines being written
# by hand. Pure reflection + text — it depends on neither pybind11 nor nanobind — so
# this is an unconditional INTERFACE target over the header-only core. Generate a
# header with welder_generate_trampolines() (see cmake/WelderTrampolines.cmake).
add_library(welder_trampolines INTERFACE)
add_library(welder::trampolines ALIAS welder_trampolines)
target_link_libraries(welder_trampolines INTERFACE welder_headers)
