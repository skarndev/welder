# Builds an importable Python extension module from an annotated struct, consuming
# welder header-only (welder::headers + the pybind11 rod).
#
# CMake-native Python_add_library + pybind11::headers (rather than
# pybind11_add_module) because pybind11 2.13's CMake helper is incompatible with
# the very recent CMake/FindPython here, and the native path keeps module-build
# flags under our control.
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)

Python_add_library(welder_poc MODULE WITH_SOABI example.cpp)
target_compile_features(welder_poc PRIVATE cxx_std_26)
target_link_libraries(welder_poc PRIVATE welder::headers welder::pybind11 welder_warnings)
# pybind11 types are hidden-visibility; a module TU touching them must be too, or
# default-visibility capturing lambdas trip -Wattributes. pybind11_add_module would
# set this; the native Python_add_library path does not, so set it explicitly.
set_target_properties(welder_poc PROPERTIES
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# Opt-in .pyi stubs (see examples/CMakeLists.txt for WELDER_STUBGEN_PYTHON).
if(WELDER_BUILD_STUBS AND WELDER_STUBGEN_PYTHON)
  welder_pybind11_generate_stubs(welder_poc PYTHON ${WELDER_STUBGEN_PYTHON})
endif()