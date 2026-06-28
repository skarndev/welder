# Builds the test extension(s) and runs the pytest suite (uv-managed) via CTest.
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)
find_program(UV_EXECUTABLE uv)

# Build one test extension from cpp/bindings.cpp. The target name doubles as the
# importable Python module name (PYBIND11_MODULE(WELDER_TEST_MODNAME)).
function(welder_add_test_module tgt)
  cmake_parse_arguments(A "HEADER_ONLY" "" "" ${ARGN})
  Python_add_library(${tgt} MODULE WITH_SOABI cpp/bindings.cpp)
  target_compile_features(${tgt} PRIVATE cxx_std_26)
  target_compile_definitions(${tgt} PRIVATE WELDER_TEST_MODNAME=${tgt})
  if(A_HEADER_ONLY)
    target_compile_definitions(${tgt} PRIVATE WELDER_TEST_HEADER_ONLY)
    target_link_libraries(${tgt} PRIVATE welder::headers welder::python)
  else()
    target_link_libraries(${tgt} PRIVATE welder::module welder::python)
  endif()
endfunction()

# Header-only variant is always available; module variant needs the module build.
set(_variants welder_test_bindings_ho)
welder_add_test_module(welder_test_bindings_ho HEADER_ONLY)
if(WELDER_BUILD_MODULE)
  welder_add_test_module(welder_test_bindings)
  list(APPEND _variants welder_test_bindings)
endif()

if(NOT UV_EXECUTABLE)
  message(WARNING "welder: 'uv' not found; skipping Python tests.")
  return()
endif()

# Prepare the test environment (pytest, mypy) once. We deliberately use uv's
# managed CPython rather than the build's Homebrew framework Python: uv rejects
# the latter (its platform.mac_ver() is empty). That's fine for importing our
# extensions — on macOS they use dynamic symbol lookup (no libpython link), so
# they load in any ABI-compatible CPython of the same minor version (3.14).
message(STATUS "welder: syncing uv test environment...")
execute_process(
  COMMAND ${UV_EXECUTABLE} sync --project ${CMAKE_CURRENT_SOURCE_DIR}
          --python 3.14 --python-preference only-managed
  RESULT_VARIABLE _uv_res
  OUTPUT_VARIABLE _uv_out ERROR_VARIABLE _uv_out)
if(NOT _uv_res EQUAL 0)
  message(WARNING "welder: 'uv sync' failed; skipping Python tests:\n${_uv_out}")
  return()
endif()
set(_venv_python ${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python)

# One CTest entry per variant; each runs the full suite against its module.
foreach(variant IN LISTS _variants)
  add_test(
    NAME pytest.${variant}
    COMMAND ${_venv_python} -m pytest ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(pytest.${variant} PROPERTIES
    ENVIRONMENT
      "WELDER_TEST_MODULE=${variant};PYTHONPATH=$<TARGET_FILE_DIR:${variant}>")
endforeach()
