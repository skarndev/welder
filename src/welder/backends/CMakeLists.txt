# Header-only language backends. Each backend is a thin adapter consuming the
# same core resolution API (welded_for / member_bound / public_bases) plus its
# own target framework. Two Python backends are implemented (pybind11, nanobind),
# sharing the docstring styles in <welder/backends/python/doc_style.hpp>; Lua is
# planned (it will gain its own <welder/backends/NAME/backend.hpp> here).

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
