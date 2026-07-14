# Cookbook 08 — tack welding: greedily bind a third-party library that carries no
# welder annotations. See docs/content/cookbook/tack-welding.md.
cookbook_add_pybind11_module(fastvec bindings.cpp)

add_test(NAME cookbook.08-tack-welding
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.08-tack-welding PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:fastvec>")