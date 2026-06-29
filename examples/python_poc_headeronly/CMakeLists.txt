# Header-only consumption of welder (no module). Proves the fallback path.
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)

Python_add_library(welder_poc_ho MODULE WITH_SOABI example.cpp)
target_compile_features(welder_poc_ho PRIVATE cxx_std_26)
# welder::headers (header-only core) instead of welder::module.
target_link_libraries(welder_poc_ho PRIVATE welder::headers welder::pybind11)
