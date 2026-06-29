# pybind11 backend tests. Build the test extension twice from one source —
# module-form (`import welder;`) and header-only — then run the shared behavioral
# specs (tests/test_*.py) against each. The target name doubles as the importable
# Python module name (PYBIND11_MODULE(WELDER_TEST_MODNAME)).
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)

function(welder_add_pybind11_test_module tgt)
  cmake_parse_arguments(A "HEADER_ONLY" "" "" ${ARGN})
  # bindings.cpp is the only TU; the per-spec-file case headers it #includes are
  # listed so they show in IDEs (header deps are tracked via compiler depfiles).
  Python_add_library(${tgt} MODULE WITH_SOABI
    cpp/bindings.cpp
    cpp/resolution.hpp cpp/methods.hpp cpp/inheritance.hpp cpp/namespace.hpp
    cpp/doc.hpp)
  target_compile_features(${tgt} PRIVATE cxx_std_26)
  target_compile_definitions(${tgt} PRIVATE WELDER_TEST_MODNAME=${tgt})
  if(A_HEADER_ONLY)
    target_compile_definitions(${tgt} PRIVATE WELDER_TEST_HEADER_ONLY)
    target_link_libraries(${tgt} PRIVATE welder::headers welder::pybind11)
  else()
    target_link_libraries(${tgt} PRIVATE welder::module welder::pybind11)
  endif()
endfunction()

# Header-only variant is always available; module variant needs the module build.
set(_variants welder_test_pybind11_ho)
welder_add_pybind11_test_module(welder_test_pybind11_ho HEADER_ONLY)
if(WELDER_BUILD_MODULE)
  welder_add_pybind11_test_module(welder_test_pybind11)
  list(APPEND _variants welder_test_pybind11)
endif()

# Register the shared pytest specs against each variant — but only when the uv
# environment was prepared at the tests/ root (otherwise just build the modules).
if(NOT WELDER_TEST_VENV_PYTHON)
  return()
endif()
foreach(variant IN LISTS _variants)
  add_test(
    NAME pytest.${variant}
    COMMAND ${WELDER_TEST_VENV_PYTHON} -m pytest ${WELDER_TEST_PYDIR})
  set_tests_properties(pytest.${variant} PROPERTIES
    ENVIRONMENT
      "WELDER_TEST_MODULE=${variant};PYTHONPATH=$<TARGET_FILE_DIR:${variant}>")
endforeach()
