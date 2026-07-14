# Cookbook 09 — custom traversal: a bespoke resolution (greedy minus the
# library's private convention) injected via basic_carriage.
# See docs/content/cookbook/custom-traversal.md.
cookbook_add_pybind11_module(sensors bindings.cpp)

add_test(NAME cookbook.09-custom-traversal
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.09-custom-traversal PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:sensors>")