# Python binding tests. The behavioral specs (test_*.py + conftest.py, here) assert
# welder's backend-agnostic semantics — which members bind, inheritance, namespaces
# — so they are shared across the Python backends: each backend subdirectory builds
# its extension(s) and registers CTest entries that run these specs against the
# built module, selected at runtime by the WELDER_TEST_MODULE env var (read in
# conftest.py). With two backends the shared specs double as a consistency check.
#
# The uv environment (pytest, mypy, pybind11-stubgen, lark) and the shared C++ case
# tree are prepared by the parent (tests/CMakeLists.txt) and inherited here as
# WELDER_TEST_VENV_PYTHON / WELDER_TEST_PYDIR / WELDER_TEST_COMMON_CPP.
# Generate the backend-neutral trampoline header for the shared gen_trampolines cases
# ONCE (welder::rods::trampolines). Both extensions compile the same generated header
# in — a cross-backend consistency check that the generated trampolines dispatch
# identically, and a self-test of the trampoline-generator rod. The generator TU
# reflects the welded types and emits the header; the modules #include it and bind the
# namespace. Exposed to the backend subdirectories (added below) via directory-scope
# variables they inherit.
welder_generate_trampolines(welder_test_gen_trampolines
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gen_trampolines_gen.cpp
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen_trampolines.trampolines.hpp
  DEPENDS ${WELDER_TEST_COMMON_CPP}/gen_trampolines.hpp)
target_include_directories(welder_test_gen_trampolines_gen
  PRIVATE ${WELDER_TEST_COMMON_CPP})
set(WELDER_GEN_TRAMPOLINES_TARGET welder_test_gen_trampolines)
set(WELDER_GEN_TRAMPOLINES_DIR ${CMAKE_CURRENT_BINARY_DIR})

# Generate the backend-neutral OPAQUE-CONTAINER header for the shared gen_opaque cases
# ONCE (welder::rods::opaque_containers): the WELDER_OPAQUE(...) declarations + welded
# aliases that bind those types' containers by reference. Both extensions #include the
# same generated header — a self-test of the generator rod and a cross-backend
# consistency check. The generator TU reflects the welded types and emits the header;
# the modules #include it (after gen_opaque.hpp + the backend rod) and bind the namespace.
welder_generate_opaque_containers(welder_test_gen_opaque
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/gen_opaque_gen.cpp
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/gen_opaque.opaque.hpp
  DEPENDS ${WELDER_TEST_COMMON_CPP}/gen_opaque.hpp)
target_include_directories(welder_test_gen_opaque_gen
  PRIVATE ${WELDER_TEST_COMMON_CPP})
set(WELDER_GEN_OPAQUE_TARGET welder_test_gen_opaque)
set(WELDER_GEN_OPAQUE_DIR ${CMAKE_CURRENT_BINARY_DIR})

if(WELDER_BUILD_PYBIND11)
  add_subdirectory(pybind11)
endif()
if(WELDER_BUILD_NANOBIND)
  add_subdirectory(nanobind)
endif()
