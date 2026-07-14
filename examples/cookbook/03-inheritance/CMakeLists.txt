# Cookbook 03 — inheritance: native welded bases vs flattened mixins.
# See docs/content/cookbook/inheritance.md.
cookbook_add_pybind11_module(garage garage.cpp)

add_test(NAME cookbook.03-inheritance
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.03-inheritance PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:garage>")