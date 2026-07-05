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
if(WELDER_BUILD_PYBIND11)
  add_subdirectory(pybind11)
endif()
if(WELDER_BUILD_NANOBIND)
  add_subdirectory(nanobind)
endif()
