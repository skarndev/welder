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
# (a compile check) but skip registering CTest entries. WELDER_TEST_COMMON_CPP is
# the backend-neutral C++ case tree, shared by every backend's bindings.cpp.
set(WELDER_TEST_PYDIR ${CMAKE_CURRENT_SOURCE_DIR})
set(WELDER_TEST_COMMON_CPP ${CMAKE_CURRENT_SOURCE_DIR}/common/cpp)
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

# Backend-abstraction probe (backend-agnostic): a minimal in-file backend that
# satisfies welder::backend using ONLY the core headers (no pybind11), driven
# through all three generic drivers. Proves the core stays decoupled from any
# concrete backend. Built as an EXCLUDE_FROM_ALL object library that must SUCCEED
# to compile; the CTest builds just that target (no WILL_FAIL). Needs only the
# compiler — no Python — so it is registered before the uv-env gate.
add_library(welder_backend_probe OBJECT EXCLUDE_FROM_ALL core/backend_probe.cpp)
target_compile_features(welder_backend_probe PRIVATE cxx_std_26)
target_link_libraries(welder_backend_probe PRIVATE welder::headers)
add_test(NAME compile.backend_probe
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_backend_probe --config $<CONFIG>)

# Template ↔ annotation semantics (compile-only): annotations on a template
# declaration are readable through every *instantiation* (primary / partial /
# explicit precedence; member, weld/mark and parameter annotations included) —
# only the uninstantiated template reflection refuses annotations_of (P2996).
# static_asserts lock the whole matrix; same compile-to-pass shape as above.
add_library(welder_template_annotations OBJECT EXCLUDE_FROM_ALL
  core/template_annotations.cpp)
target_compile_features(welder_template_annotations PRIVATE cxx_std_26)
target_link_libraries(welder_template_annotations PRIVATE welder::headers)
add_test(NAME compile.template_annotations
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_template_annotations --config $<CONFIG>)

# Docstring dedent semantics (compile-only): welder::cleandoc (doc.hpp) mirrors
# Python inspect.cleandoc / PEP 257 so multiline docs can be indented to match the
# source without the indentation leaking into __doc__. static_asserts lock the
# exact cases; same compile-to-pass shape as above (no Python needed).
add_library(welder_doc_cleandoc OBJECT EXCLUDE_FROM_ALL core/doc_cleandoc.cpp)
target_compile_features(welder_doc_cleandoc PRIVATE cxx_std_26)
target_link_libraries(welder_doc_cleandoc PRIVATE welder::headers)
add_test(NAME compile.doc_cleandoc
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_doc_cleandoc --config $<CONFIG>)

# Doxygen INPUT_FILTER tests (text-level; python3, optionally doxygen).
add_subdirectory(doxyfilter)

# Per-backend test trees (each builds its extensions + registers CTest). Both
# consume the shared specs (tests/test_*.py) and the shared C++ case tree
# (tests/common/cpp), so once both are built the specs double as a cross-backend
# consistency check.
if(WELDER_BUILD_PYBIND11)
  add_subdirectory(pybind11)
endif()
if(WELDER_BUILD_NANOBIND)
  add_subdirectory(nanobind)
endif()
# The sol2 (Lua) tree reuses the same shared C++ cases, asserted from Lua rather
# than pytest — it needs no uv/Python env, so it registers independently.
if(WELDER_BUILD_SOL2)
  add_subdirectory(sol2)
endif()
