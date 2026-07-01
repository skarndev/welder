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
    cpp/doc.hpp cpp/operators.hpp cpp/trust.hpp cpp/caster.hpp)
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

# Negative-compile cases: welder must *reject*, with a hard compile error, any
# attempt to bind a type pybind11 cannot convert to a meaningful Python value.
# Each source is an EXCLUDE_FROM_ALL object library that must FAIL to compile; the
# CTest builds just that target and is marked WILL_FAIL. These need only the
# compiler (no Python runtime), so they are registered before the uv-env gate.
foreach(neg operand_not_welded container_of_unwelded)
  add_library(welder_neg_${neg} OBJECT EXCLUDE_FROM_ALL cpp/neg/${neg}.cpp)
  target_compile_features(welder_neg_${neg} PRIVATE cxx_std_26)
  target_link_libraries(welder_neg_${neg} PRIVATE welder::pybind11)
  add_test(NAME negcompile.${neg}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target welder_neg_${neg} --config $<CONFIG>)
  set_tests_properties(negcompile.${neg} PROPERTIES WILL_FAIL TRUE)
endforeach()

# Everything below needs the uv environment that was prepared at the tests/ root
# (it carries pytest, mypy, and pybind11-stubgen). Without it, just build the
# modules — they double as a compile check.
if(NOT WELDER_TEST_VENV_PYTHON)
  return()
endif()

# pybind11-stubgen writes each variant's package under here (named after its
# module: welder_test_pybind11/ and welder_test_pybind11_ho/), so they coexist.
set(_stubs_dir ${CMAKE_CURRENT_BINARY_DIR}/stubs)

foreach(variant IN LISTS _variants)
  # Behavioral specs: import the compiled extension and assert what binds.
  # -p no:mypy-testing so this run only executes the runtime specs — the type-level
  # cases (test_types.mypy-testing) are run separately, with stubs on MYPYPATH.
  add_test(
    NAME pytest.${variant}
    COMMAND ${WELDER_TEST_VENV_PYTHON} -m pytest ${WELDER_TEST_PYDIR} -p no:mypy-testing
    WORKING_DIRECTORY ${WELDER_TEST_PYDIR})
  set_tests_properties(pytest.${variant} PROPERTIES
    ENVIRONMENT
      "WELDER_TEST_MODULE=${variant};PYTHONPATH=$<TARGET_FILE_DIR:${variant}>")

  if(WELDER_BUILD_STUBS)
    # Emit .pyi stubs from the freshly built extension via pybind11-stubgen.
    welder_pybind11_generate_stubs(${variant}
      PYTHON ${WELDER_TEST_VENV_PYTHON}
      OUTPUT_DIR ${_stubs_dir})

    # ...and type-check them: mypy --strict over the generated package proves the
    # stubs are internally consistent and well-typed. Run from the tests/ dir so
    # mypy picks up [tool.mypy] from pyproject.toml. Two error codes are disabled
    # for known, accepted artifacts of binding a type with comparison operators
    # (not stub defects): `override` — operator== yields __eq__(self, other: Vec),
    # which narrows object.__eq__; `assignment` — defining __eq__ makes the type
    # unhashable, so the stub records __hash__ = None (vs object's Callable).
    add_test(
      NAME stubcheck.${variant}
      COMMAND ${WELDER_TEST_VENV_PYTHON} -m mypy
              --disable-error-code=override --disable-error-code=assignment
              ${_stubs_dir}/${variant}
      WORKING_DIRECTORY ${WELDER_TEST_PYDIR})
  endif()
endforeach()

# Type-level cases: tests/test_types.mypy-testing imports the backend-neutral name
# `welder_test` and asserts revealed types, so a wrong stub type fails. Expose the
# module-form stubs under that canonical name (rename-safe — the tree uses only
# relative imports) and run the shared cases against them via pytest-mypy-testing.
# Skip when only the header-only variant is built (no module-form stubs).
if(WELDER_BUILD_STUBS AND WELDER_BUILD_MODULE)
  set(_canon_stubs ${CMAKE_CURRENT_BINARY_DIR}/typing-stubs)
  add_custom_command(TARGET welder_test_pybind11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_canon_stubs}/welder_test
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${_stubs_dir}/welder_test_pybind11 ${_canon_stubs}/welder_test
    VERBATIM
    COMMENT "welder: exposing pybind11 stubs as `welder_test` for shared typing cases")

  add_test(
    NAME typingcases.pybind11
    COMMAND ${WELDER_TEST_VENV_PYTHON} -m pytest ${WELDER_TEST_PYDIR}/test_types.mypy-testing
    WORKING_DIRECTORY ${WELDER_TEST_PYDIR})
  set_tests_properties(typingcases.pybind11 PROPERTIES
    ENVIRONMENT "MYPYPATH=${_canon_stubs}")
endif()
