# Cookbook 06 — templates: weld class/function template instantiations under
# explicit names. See docs/content/cookbook/templates.md.
cookbook_add_pybind11_module(boxes boxes.cpp)

add_test(NAME cookbook.06-templates
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.06-templates PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:boxes>")