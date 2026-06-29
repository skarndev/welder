# Builds an importable Python extension module from an annotated struct.
#
# CMake-native Python_add_library + pybind11::headers (rather than
# pybind11_add_module) because pybind11 2.13's CMake helper is incompatible with
# the very recent CMake/FindPython here, and the native path keeps module-build
# flags under our control.
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)

Python_add_library(welder_poc MODULE WITH_SOABI example.cpp)
target_compile_features(welder_poc PRIVATE cxx_std_26)
# welder::module  -> `import welder;`   welder::pybind11 -> the pybind11 backend
target_link_libraries(welder_poc PRIVATE welder::module welder::pybind11)
