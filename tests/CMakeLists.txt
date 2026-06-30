# Python binding tests. The behavioral specs (test_*.py + conftest.py) assert
# welder's *backend-agnostic* semantics — which members bind, inheritance,
# namespaces — so they are shared across backends and live here. Each backend
# subdirectory (tests/pybind11, later tests/nanobind, ...) builds its own
# extension(s) and registers CTest entries that run these shared specs against the
# built module, selected at runtime by the WELDER_TEST_MODULE env var (read in
# conftest.py). Once a second backend exists, the shared specs double as a
# cross-backend consistency check.
find_program(UV_EXECUTABLE uv)

# Handed down to the backend subdirectories. WELDER_TEST_VENV_PYTHON stays empty
# if the uv env can't be prepared: backends then still *build* their extensions
# (a compile check) but skip registering CTest entries.
set(WELDER_TEST_PYDIR ${CMAKE_CURRENT_SOURCE_DIR})
set(WELDER_TEST_VENV_PYTHON "")

if(NOT UV_EXECUTABLE)
  message(WARNING "welder: 'uv' not found; Python tests will be skipped.")
else()
  # Prepare the (shared) uv environment once. We deliberately use uv's managed
  # CPython rather than the build's Homebrew framework Python: uv rejects the
  # latter (its platform.mac_ver() is empty). That's fine for importing our
  # extensions — on macOS they use dynamic symbol lookup (no libpython link), so
  # they load in any ABI-compatible CPython of the same minor version (3.14).
  message(STATUS "welder: syncing uv test environment...")
  execute_process(
    COMMAND ${UV_EXECUTABLE} sync --project ${CMAKE_CURRENT_SOURCE_DIR}
            --python 3.14 --python-preference only-managed
    RESULT_VARIABLE _uv_res
    OUTPUT_VARIABLE _uv_out ERROR_VARIABLE _uv_out)
  if(_uv_res EQUAL 0)
    set(WELDER_TEST_VENV_PYTHON ${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python)
  else()
    message(WARNING "welder: 'uv sync' failed; Python tests will be skipped:\n${_uv_out}")
  endif()
endif()

# Type-check the test sources themselves (backend-agnostic): a plain strict mypy
# over the .py specs + conftest (the `files` list in pyproject.toml; no .venv, no
# stubs needed — the specs reach the extension through a ModuleType fixture). The
# type-level cases (test_types.mypy-testing) are checked per-backend instead.
if(WELDER_TEST_VENV_PYTHON)
  add_test(
    NAME mypy.tests
    COMMAND ${WELDER_TEST_VENV_PYTHON} -m mypy
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Per-backend test trees (each builds its extensions + registers CTest).
if(WELDER_BUILD_PYBIND11)
  add_subdirectory(pybind11)
endif()
