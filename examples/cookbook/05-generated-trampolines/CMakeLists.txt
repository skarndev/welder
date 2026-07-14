# Cookbook 05 — generated trampolines: welder_generate_trampolines() builds the
# reflection generator (gen.cpp), runs it into machines.trampolines.hpp, and the
# binding TU compiles the result. See docs/content/cookbook/generated-trampolines.md.
welder_generate_trampolines(machines_trampolines
  SOURCES gen.cpp
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/machines.trampolines.hpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/machines.hpp)

cookbook_add_pybind11_module(machines machines.cpp)
target_include_directories(machines PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(machines machines_trampolines)

add_test(NAME cookbook.05-generated-trampolines
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.05-generated-trampolines PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:machines>")