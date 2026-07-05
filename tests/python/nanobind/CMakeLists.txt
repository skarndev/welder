# nanobind backend tests. Build the test extension twice from one source —
# module-form (`import welder;`) and header-only — then run the *shared* behavioral
# specs (tests/test_*.py) against each, exactly as the pybind11 tree does. The specs
# are backend-agnostic and select the module under test via WELDER_TEST_MODULE, so
# they double as a cross-backend consistency check. The C++ case groups are the
# shared tree in tests/common/cpp; only trust.hpp / caster.hpp are nanobind-specific
# (here in cpp/). The target name doubles as the importable module name
# (NB_MODULE(WELDER_TEST_MODNAME)).
#
# nanobind ships its runtime as source compiled into each extension via
# nanobind_add_module (not a plain link library), so the extensions are created
# with that helper rather than Python_add_library.
find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED)

function(welder_add_nanobind_test_module tgt)
  cmake_parse_arguments(A "HEADER_ONLY" "" "" ${ARGN})
  # bindings.cpp is the only TU; the case headers it #includes are listed so they
  # show in IDEs (header deps are tracked via compiler depfiles). The backend-neutral
  # groups live in ../common/cpp (shared with pybind11); trust and caster are
  # nanobind-specific and live alongside bindings.cpp.
  nanobind_add_module(${tgt}
    cpp/bindings.cpp cpp/trust.hpp cpp/caster.hpp
    ${WELDER_TEST_COMMON_CPP}/resolution.hpp ${WELDER_TEST_COMMON_CPP}/methods.hpp
    ${WELDER_TEST_COMMON_CPP}/inheritance.hpp ${WELDER_TEST_COMMON_CPP}/namespace.hpp
    ${WELDER_TEST_COMMON_CPP}/doc.hpp ${WELDER_TEST_COMMON_CPP}/operators.hpp
    ${WELDER_TEST_COMMON_CPP}/enums.hpp)
  target_include_directories(${tgt} PRIVATE ${WELDER_TEST_COMMON_CPP})
  target_compile_features(${tgt} PRIVATE cxx_std_26)
  target_compile_definitions(${tgt} PRIVATE WELDER_TEST_MODNAME=${tgt})
  if(A_HEADER_ONLY)
    target_compile_definitions(${tgt} PRIVATE WELDER_TEST_HEADER_ONLY)
    target_link_libraries(${tgt} PRIVATE welder::headers welder::nanobind)
  else()
    target_link_libraries(${tgt} PRIVATE welder::module welder::nanobind)
  endif()
endfunction()

# Header-only variant is always available; module variant needs the module build.
set(_variants welder_test_nanobind_ho)
welder_add_nanobind_test_module(welder_test_nanobind_ho HEADER_ONLY)
if(WELDER_BUILD_MODULE)
  welder_add_nanobind_test_module(welder_test_nanobind)
  list(APPEND _variants welder_test_nanobind)
endif()

# Negative-compile cases: welder must *reject*, with a hard compile error, any
# attempt to bind a type nanobind cannot convert to a meaningful Python value. Each
# source is an EXCLUDE_FROM_ALL object library that must FAIL to compile; the CTest
# builds just that target and is marked WILL_FAIL. These need only the compiler (no
# Python runtime), so they are registered before the uv-env gate. welder::nanobind
# carries the nanobind + welder includes; Python::Module adds the Python.h include.
foreach(neg operand_not_welded container_of_unwelded)
  add_library(welder_neg_nanobind_${neg} OBJECT EXCLUDE_FROM_ALL cpp/neg/${neg}.cpp)
  target_compile_features(welder_neg_nanobind_${neg} PRIVATE cxx_std_26)
  target_link_libraries(welder_neg_nanobind_${neg} PRIVATE welder::nanobind Python::Module)
  add_test(NAME negcompile.nanobind.${neg}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target welder_neg_nanobind_${neg} --config $<CONFIG>)
  set_tests_properties(negcompile.nanobind.${neg} PROPERTIES WILL_FAIL TRUE)
endforeach()

# .pyi stubs, via nanobind's *bundled* stubgen (nanobind_add_stub from
# nanobind-config.cmake). Unlike the pybind11 tree — which needs pybind11-stubgen
# from the uv env — this needs no extra package: stubgen.py is stdlib-only on
# Python >= 3.11, run through the build's Python_EXECUTABLE, which loads the
# extension by dynamic symbol lookup. So stubs build independently of the uv env
# (gated only by WELDER_BUILD_STUBS). RECURSIVE walks the submodules welder created
# (one per case group), writing a package tree ${_stubs_dir}/<variant>/; the py.typed
# marker doubles as the build-time output CMake tracks. welder docstrings (class,
# property, function Args:/Returns:) flow into the stubs.
set(_stubs_dir ${CMAKE_CURRENT_BINARY_DIR}/stubs)
if(WELDER_BUILD_STUBS)
  foreach(variant IN LISTS _variants)
    nanobind_add_stub(${variant}_stub
      MODULE ${variant}
      RECURSIVE
      OUTPUT_PATH ${_stubs_dir}
      MARKER_FILE ${_stubs_dir}/${variant}/py.typed
      PYTHON_PATH $<TARGET_FILE_DIR:${variant}>
      DEPENDS ${variant})
  endforeach()
endif()

# Everything below needs the uv environment prepared at the tests/ root (pytest,
# mypy). Without it, just build the modules + stubs — they double as a compile check.
if(NOT WELDER_TEST_VENV_PYTHON)
  return()
endif()

foreach(variant IN LISTS _variants)
  # Behavioral specs: import the compiled extension and assert what binds.
  # -p no:mypy-testing so this run only executes the runtime specs.
  add_test(
    NAME pytest.${variant}
    COMMAND ${WELDER_TEST_VENV_PYTHON} -m pytest ${WELDER_TEST_PYDIR} -p no:mypy-testing
    WORKING_DIRECTORY ${WELDER_TEST_PYDIR})
  set_tests_properties(pytest.${variant} PROPERTIES
    ENVIRONMENT
      "WELDER_TEST_MODULE=${variant};PYTHONPATH=$<TARGET_FILE_DIR:${variant}>")

  if(WELDER_BUILD_STUBS)
    # ...and type-check the generated stub package: mypy --strict proves it is
    # internally consistent and well-typed. Two error codes are disabled for the
    # same accepted artifacts of binding a type with comparison operators as in the
    # pybind11 tree: `override` (operator== narrows object.__eq__) and `assignment`
    # (defining __eq__ sets __hash__ = None vs object's Callable).
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
# module-form stubs under that canonical name (rename-safe — nanobind's stub tree
# uses only relative imports, `from . import ...`) and run the shared cases against
# them via pytest-mypy-testing. Skip when only the header-only variant is built (no
# module-form stubs). The copy is attached to the stub *target*'s POST_BUILD (not
# the extension's): nanobind generates stubs in a separate custom target that only
# DEPENDS on the extension, so copying from the extension's POST_BUILD could race
# ahead of stub generation.
if(WELDER_BUILD_STUBS AND WELDER_BUILD_MODULE)
  set(_canon_stubs ${CMAKE_CURRENT_BINARY_DIR}/typing-stubs)
  add_custom_command(TARGET welder_test_nanobind_stub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_canon_stubs}/welder_test
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${_stubs_dir}/welder_test_nanobind ${_canon_stubs}/welder_test
    VERBATIM
    COMMENT "welder: exposing nanobind stubs as `welder_test` for shared typing cases")

  add_test(
    NAME typingcases.nanobind
    COMMAND ${WELDER_TEST_VENV_PYTHON} -m pytest ${WELDER_TEST_PYDIR}/test_types.mypy-testing
    WORKING_DIRECTORY ${WELDER_TEST_PYDIR})
  set_tests_properties(typingcases.nanobind PROPERTIES
    ENVIRONMENT "MYPYPATH=${_canon_stubs}")
endif()
