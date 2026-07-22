# Cookbook 10 — containers by reference: welder_generate_opaque_containers() builds the
# reflection generator (gen.cpp), runs it into scene.opaque.hpp, and the binding TU
# compiles the result. See docs/content/cookbook/containers.md.
welder_generate_opaque_containers(scene_opaque
  SOURCES gen.cpp
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/scene.opaque.hpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scene.hpp)
target_include_directories(scene_opaque_gen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

cookbook_add_pybind11_module(scene scene.cpp)
target_include_directories(scene PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(scene scene_opaque)

add_test(NAME cookbook.10-containers
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.10-containers PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:scene>")
