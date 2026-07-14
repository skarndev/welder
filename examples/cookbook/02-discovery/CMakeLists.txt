# Cookbook 02 — discovery: policies, marks, nested namespaces and weld_as, driven
# through one WELDER_MODULE. See docs/content/cookbook/discovery.md.
cookbook_add_pybind11_module(inventory inventory.cpp)

add_test(NAME cookbook.02-discovery
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.02-discovery PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:inventory>")