# Builds the `shapes` Python module from an annotated namespace via WELDER_MODULE.
# Header-only consumption (welder::headers), so it needs no module build.
#
# CMake-native Python_add_library + pybind11::headers (rather than
# pybind11_add_module) for the same reason as the other examples — see
# examples/python_poc/CMakeLists.txt.
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)

# The target/module name must match the namespace token in WELDER_MODULE(shapes,
# ...) so Python's PyInit_shapes is what gets imported.
Python_add_library(shapes MODULE WITH_SOABI example.cpp)
target_compile_features(shapes PRIVATE cxx_std_26)
target_link_libraries(shapes PRIVATE welder::headers welder::pybind11 welder_warnings)
# pybind11 types are hidden-visibility; a module TU touching them must be too, or
# default-visibility capturing lambdas trip -Wattributes. pybind11_add_module would
# set this; the native Python_add_library path does not, so set it explicitly.
set_target_properties(shapes PROPERTIES
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# Opt-in .pyi stubs (see examples/CMakeLists.txt for WELDER_STUBGEN_PYTHON).
if(WELDER_BUILD_STUBS AND WELDER_STUBGEN_PYTHON)
  welder_pybind11_generate_stubs(shapes PYTHON ${WELDER_STUBGEN_PYTHON})
endif()
