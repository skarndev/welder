# Header-only language backends. Each backend is a thin adapter consuming the
# same core resolution API (welded_for / member_bound / public_bases) plus its
# own target framework. Today only the pybind11 backend is implemented; nanobind
# and Lua are planned (they will gain their own <welder/backends/NAME.hpp> here).

# --- welder::pybind11 : pybind11 Python backend ------------------------------
if(WELDER_BUILD_PYBIND11)
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(pybind11 REQUIRED)

  add_library(welder_pybind11 INTERFACE)
  add_library(welder::pybind11 ALIAS welder_pybind11)
  target_link_libraries(welder_pybind11 INTERFACE
    welder_headers pybind11::headers Python::Module)
endif()
