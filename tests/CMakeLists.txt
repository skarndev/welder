# welder's tests split by target language: the backend-neutral C++ case tree
# (common/cpp) and the compile-only core checks live here; the Python backends +
# their pytest specs live under python/, the Lua backend + its busted specs under
# lua/. The C++ cases are shared across all three backends (welded for both
# lang::py and lang::lua) as a cross-backend consistency check.
# The type-level cases (test_types.mypy-testing) assert mypy's exact reveal_type
# spelling, which is pinned to a specific mypy (2.x). Turn them off where that mypy
# can't be installed — notably the mingw python, where mypy 2.x won't build (its
# Rust build dep has no mingw wheel). The stubs those cases check are byte-identical
# across platforms (same pybind11-stubgen), so Linux/macOS cover them fully; Windows
# still runs stubcheck (stub well-formedness) and the behavioral specs.
option(WELDER_TEST_TYPINGCASES "Run the mypy-version-sensitive type-level cases" ON)

find_program(UV_EXECUTABLE uv)

# A ready-made interpreter already carrying the test deps (pytest, mypy,
# pytest-mypy-testing, pybind11-stubgen, lark). Set it to bypass uv where uv can't
# build the env: MSYS2's mingw python, which uv refuses outright ("Unknown
# operating system: mingw_..."), and whose ABI a gcc-built .pyd needs anyway (uv's
# managed CPython is MSVC-ABI). Empty = provision the env here via uv, the default
# everywhere else.
set(WELDER_TEST_VENV_PYTHON "" CACHE FILEPATH
  "Interpreter (already carrying the test deps) to run the Python tests with; \
empty = provision it here via uv")

# Handed down to the language subdirectories. WELDER_TEST_VENV_PYTHON stays empty
# if the env can't be prepared: backends then still *build* their extensions (a
# compile check) but skip registering CTest entries. WELDER_TEST_COMMON_CPP is the
# backend-neutral C++ case tree, shared by every backend's bindings.cpp. The env is
# prepared here (not under python/) because the doxyfilter tests also use it (for
# `lark`); it manages the pyproject that now lives in python/.
set(WELDER_TEST_PYDIR ${CMAKE_CURRENT_SOURCE_DIR}/python)
set(WELDER_TEST_COMMON_CPP ${CMAKE_CURRENT_SOURCE_DIR}/common/cpp)

if(WELDER_TEST_VENV_PYTHON)
  message(STATUS "welder: using provided test interpreter: ${WELDER_TEST_VENV_PYTHON}")
elseif(NOT UV_EXECUTABLE)
  message(WARNING "welder: 'uv' not found; Python tests will be skipped.")
else()
  # Prepare the (shared) uv environment once. We deliberately use uv's managed
  # CPython rather than the build's Homebrew framework Python: uv rejects the
  # latter (its platform.mac_ver() is empty). That's fine for importing our
  # extensions — on macOS/Linux they load in any ABI-compatible CPython of the same
  # minor (WELDER_PYTHON_VERSION) via dynamic symbol lookup.
  message(STATUS "welder: syncing uv test environment (Python ${WELDER_PYTHON_VERSION})...")
  execute_process(
    COMMAND ${UV_EXECUTABLE} sync --project ${WELDER_TEST_PYDIR}
            --python ${WELDER_PYTHON_VERSION} --python-preference only-managed
    RESULT_VARIABLE _uv_res
    OUTPUT_VARIABLE _uv_out ERROR_VARIABLE _uv_out)
  if(_uv_res EQUAL 0)
    set(WELDER_TEST_VENV_PYTHON ${WELDER_TEST_PYDIR}/.venv/bin/python)
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
    WORKING_DIRECTORY ${WELDER_TEST_PYDIR})
endif()

# Rod-abstraction probe (backend-agnostic): a minimal in-file rod that
# satisfies welder::rod using ONLY the core headers (no pybind11), driven
# through every welder::welder entry point. Proves the core stays decoupled from
# any concrete rod. Built as an EXCLUDE_FROM_ALL object library that must SUCCEED
# to compile; the CTest builds just that target (no WILL_FAIL). Needs only the
# compiler — no Python — so it is registered before the uv-env gate.
add_library(welder_rod_probe OBJECT EXCLUDE_FROM_ALL core/rod_probe.cpp)
target_compile_features(welder_rod_probe PRIVATE cxx_std_26)
target_link_libraries(welder_rod_probe PRIVATE welder::headers welder_warnings)
add_test(NAME compile.rod_probe
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_rod_probe --config $<CONFIG>)

# Template ↔ annotation semantics (compile-only): annotations on a template
# declaration are readable through every *instantiation* (primary / partial /
# explicit precedence; member, weld/mark and parameter annotations included) —
# only the uninstantiated template reflection refuses annotations_of (P2996).
# static_asserts lock the whole matrix; same compile-to-pass shape as above.
add_library(welder_template_annotations OBJECT EXCLUDE_FROM_ALL
  core/template_annotations.cpp)
target_compile_features(welder_template_annotations PRIVATE cxx_std_26)
target_link_libraries(welder_template_annotations PRIVATE welder::headers welder_warnings)
add_test(NAME compile.template_annotations
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_template_annotations --config $<CONFIG>)

# Docstring dedent semantics (compile-only): welder::cleandoc (doc.hpp) mirrors
# Python inspect.cleandoc / PEP 257 so multiline docs can be indented to match the
# source without the indentation leaking into __doc__. static_asserts lock the
# exact cases; same compile-to-pass shape as above (no Python needed).
add_library(welder_doc_cleandoc OBJECT EXCLUDE_FROM_ALL core/doc_cleandoc.cpp)
target_compile_features(welder_doc_cleandoc PRIVATE cxx_std_26)
target_link_libraries(welder_doc_cleandoc PRIVATE welder::headers welder_warnings)
add_test(NAME compile.doc_cleandoc
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_doc_cleandoc --config $<CONFIG>)

# Python docstring styles (compile-only): the three constexpr styles the Python
# rods fold function/param/return docs into (google/numpy/sphinx). static_asserts
# lock each style's exact layout; being constexpr they need no Python and no rod —
# same compile-to-pass shape as above. (These are rod-agnostic string builders, so
# they live under core/ despite living in the python rod tree.)
add_library(welder_doc_styles OBJECT EXCLUDE_FROM_ALL core/doc_styles.cpp)
target_compile_features(welder_doc_styles PRIVATE cxx_std_26)
target_link_libraries(welder_doc_styles PRIVATE welder::headers welder_warnings)
add_test(NAME compile.doc_styles
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_doc_styles --config $<CONFIG>)

# Name-styling + weld_as semantics (compile-only): welder::naming reshapes an
# identifier into a target convention whatever its source spelling, and
# welder::name_of resolves the bound name (a [[=welder::weld_as]] override winning
# verbatim over the style). static_asserts lock the word-splitting, restyling and
# resolution; same compile-to-pass shape as above (no Python/Lua needed).
add_library(welder_naming OBJECT EXCLUDE_FROM_ALL core/naming.cpp)
target_compile_features(welder_naming PRIVATE cxx_std_26)
target_link_libraries(welder_naming PRIVATE welder::headers welder_warnings)
add_test(NAME compile.naming
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_naming --config $<CONFIG>)

# Doxygen INPUT_FILTER tests (text-level; python3, optionally doxygen).
add_subdirectory(doxyfilter)

# LuaCATS stub backend (compile + run + golden; needs only the compiler — the stub
# backend depends on neither sol2 nor a Lua runtime, so it is registered here, not
# under the sol2-gated lua/ tree).
add_subdirectory(luacats)

# Per-language test trees. python/ builds the pybind11 + nanobind extensions and
# runs the shared pytest specs against each (a cross-backend consistency check);
# lua/ builds the sol2 and/or LuaBridge3 extensions and runs the *same* busted specs
# against each (a cross-backend consistency check). All bind the *same* shared C++
# cases (tests/common/cpp).
if(WELDER_BUILD_PYBIND11 OR WELDER_BUILD_NANOBIND)
  add_subdirectory(python)
endif()
if(WELDER_BUILD_SOL2 OR WELDER_BUILD_LUABRIDGE)
  add_subdirectory(lua)
endif()
