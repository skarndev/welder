# nanobind backend tests. Build the test extension from one source (welder is
# header-only) and run the *shared* behavioral specs (tests/test_*.py) against it,
# exactly as the pybind11 tree does. The specs are backend-agnostic and select the
# module under test via WELDER_TEST_MODULE, so they double as a cross-backend
# consistency check. The C++ case groups are the shared tree in tests/common/cpp;
# only trust.hpp / caster.hpp are nanobind-specific (here in cpp/). The target name
# doubles as the importable module name (NB_MODULE(WELDER_TEST_MODNAME)).
#
# nanobind ships its runtime as source compiled into each extension via
# nanobind_add_module (not a plain link library), so the extensions are created
# with that helper rather than Python_add_library.
set(_welder_nb_py_components Interpreter Development.Module)
if(WELDER_NANOBIND_STABLE_ABI)
  list(APPEND _welder_nb_py_components Development.SABIModule)
endif()
find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED COMPONENTS ${_welder_nb_py_components})
find_package(nanobind CONFIG REQUIRED)

function(welder_add_nanobind_test_module tgt)
  # bindings.cpp is the only TU; the case headers it #includes are listed so they
  # show in IDEs (header deps are tracked via compiler depfiles). The backend-neutral
  # groups live in ../common/cpp (shared with pybind11); trust and caster are
  # nanobind-specific and live alongside bindings.cpp.
  #
  # STABLE_ABI (opt-in via WELDER_NANOBIND_STABLE_ABI) builds against Py_LIMITED_API
  # so the module links python3.dll and loads across Python minors and the compiler
  # boundary — a UCRT-MinGW gcc build then imports in a stock MSVC CPython.
  set(_nb_args "")
  if(WELDER_NANOBIND_STABLE_ABI)
    set(_nb_args STABLE_ABI)
  endif()
  nanobind_add_module(${tgt} ${_nb_args}
    cpp/bindings.cpp cpp/trust.hpp cpp/caster.hpp
    ${WELDER_TEST_COMMON_CPP}/resolution.hpp ${WELDER_TEST_COMMON_CPP}/methods.hpp
    ${WELDER_TEST_COMMON_CPP}/inheritance.hpp ${WELDER_TEST_COMMON_CPP}/namespace.hpp
    ${WELDER_TEST_COMMON_CPP}/doc.hpp ${WELDER_TEST_COMMON_CPP}/operators.hpp
    ${WELDER_TEST_COMMON_CPP}/enums.hpp ${WELDER_TEST_COMMON_CPP}/gen_trampolines.hpp
    ${WELDER_TEST_COMMON_CPP}/templates.hpp ${WELDER_TEST_COMMON_CPP}/unions.hpp)
  target_include_directories(${tgt} PRIVATE ${WELDER_TEST_COMMON_CPP})
  # The generated trampoline header (welder::rods::trampolines), shared with pybind11.
  target_include_directories(${tgt} PRIVATE ${WELDER_GEN_TRAMPOLINES_DIR})
  add_dependencies(${tgt} ${WELDER_GEN_TRAMPOLINES_TARGET})
  target_compile_features(${tgt} PRIVATE cxx_std_26)
  target_compile_definitions(${tgt} PRIVATE WELDER_TEST_MODNAME=${tgt})
  target_link_libraries(${tgt} PRIVATE welder::headers welder::nanobind welder_warnings)

  # For a distributable stable-ABI module on MinGW: fold the gcc runtimes in so the
  # .pyd is self-contained (no libstdc++-6/libgcc_s/libwinpthread on the host's
  # PATH), and override nanobind's -Os on the binding TU with -O2. gcc-16.1 has an
  # -Os codegen bug that emits an unresolved out-of-line ref to std::string's move
  # ctor (which -O2 inlines away); the later flag wins over nanobind's -Os.
  if(WELDER_NANOBIND_STABLE_ABI AND MINGW)
    target_compile_options(${tgt} PRIVATE -O2)
    target_link_options(${tgt} PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endfunction()

set(_variants welder_test_nanobind)
welder_add_nanobind_test_module(welder_test_nanobind)

# Negative-compile cases: welder must *reject*, with a hard compile error, any
# attempt to bind a type nanobind cannot convert to a meaningful Python value. Each
# source is an EXCLUDE_FROM_ALL object library that must FAIL to compile; the CTest
# builds just that target and is marked WILL_FAIL. These need only the compiler (no
# Python runtime), so they are registered before the uv-env gate. welder::nanobind
# carries the nanobind + welder includes; Python::Module adds the Python.h include.
foreach(neg operand_not_welded container_of_unwelded
            virtual_needs_trampoline trampoline_missing_override
            trampoline_ambiguous)
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
# generated stubs under that canonical name (rename-safe — nanobind's stub tree
# uses only relative imports, `from . import ...`) and run the shared cases against
# them via pytest-mypy-testing. The copy is attached to the stub *target*'s
# POST_BUILD (not the extension's): nanobind generates stubs in a separate custom
# target that only DEPENDS on the extension, so copying from the extension's
# POST_BUILD could race ahead of stub generation.
if(WELDER_BUILD_STUBS AND WELDER_TEST_TYPINGCASES)
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
