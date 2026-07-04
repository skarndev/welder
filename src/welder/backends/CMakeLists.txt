# Header-only language backends. Each backend is a thin adapter consuming the
# same core resolution API (welded_for / member_bound / public_bases) plus its
# own target framework. Two Python backends (pybind11, nanobind) share the
# docstring styles in <welder/backends/python/doc_style.hpp>; the sol2 backend
# targets Lua via <welder/backends/lua/sol2/backend.hpp>.

# --- welder::pybind11 : pybind11 Python backend ------------------------------
if(WELDER_BUILD_PYBIND11)
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
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
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
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
# not link `lua::lua` (which carries `liblua`). Create the extension with
# welder_sol2_add_module() (see cmake/WelderSol2Module.cmake), which sets the
# bare `<name>.so` output and the host-symbol link model.
if(WELDER_BUILD_SOL2)
  find_package(sol2 REQUIRED)
  find_package(lua REQUIRED)

  add_library(welder_sol2 INTERFACE)
  add_library(welder::sol2 ALIAS welder_sol2)
  target_link_libraries(welder_sol2 INTERFACE welder_headers)
  # Headers only (see note above): pull the include dirs off the imported
  # targets without linking their libraries.
  target_include_directories(welder_sol2 SYSTEM INTERFACE
    $<TARGET_PROPERTY:sol2::sol2,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:lua::lua,INTERFACE_INCLUDE_DIRECTORIES>)
  # Match the probes: enable sol2's strong safety set (type/bounds checks).
  target_compile_definitions(welder_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)
endif()
