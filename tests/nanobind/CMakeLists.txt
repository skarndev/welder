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
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
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

# Everything below needs the uv environment prepared at the tests/ root (pytest).
# Without it, just build the modules — they double as a compile check. Stub
# generation is pybind11-specific (pybind11-stubgen); nanobind has its own stubgen,
# not yet wired here, so the nanobind tree runs the behavioral specs only.
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
endforeach()
