# Cookbook 01 — hello: weld a type, an enum, a free function and a namespace
# variable, one weld_* call each. See docs/content/cookbook/hello.md.
cookbook_add_pybind11_module(hello hello.cpp)

add_test(NAME cookbook.01-hello
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.01-hello PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:hello>")