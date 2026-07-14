# Cookbook 04 — virtuals: hand-written trampolines, both discovery forms, and
# bind_flat. See docs/content/cookbook/virtuals.md.
cookbook_add_pybind11_module(robots robots.cpp)

add_test(NAME cookbook.04-virtuals
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.04-virtuals PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:robots>")