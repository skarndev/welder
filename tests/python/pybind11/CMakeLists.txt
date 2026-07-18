# pybind11 backend tests. Build the test extension from one source (welder is
# header-only) and run the shared behavioral specs (tests/test_*.py) against it.
# The target name doubles as the importable Python module name
# (PYBIND11_MODULE(WELDER_TEST_MODNAME)).
find_package(Python ${WELDER_PYTHON_VERSION} REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)

function(welder_add_pybind11_test_module tgt)
  # bindings.cpp is the only TU; the per-spec-file case headers it #includes are
  # listed so they show in IDEs (header deps are tracked via compiler depfiles).
  # The backend-neutral groups live in ../common/cpp (shared with nanobind); trust
  # and caster are pybind11-specific and live alongside bindings.cpp.
  Python_add_library(${tgt} MODULE WITH_SOABI
    cpp/bindings.cpp cpp/trust.hpp cpp/caster.hpp
    ${WELDER_TEST_COMMON_CPP}/resolution.hpp ${WELDER_TEST_COMMON_CPP}/methods.hpp
    ${WELDER_TEST_COMMON_CPP}/inheritance.hpp ${WELDER_TEST_COMMON_CPP}/namespace.hpp
    ${WELDER_TEST_COMMON_CPP}/doc.hpp ${WELDER_TEST_COMMON_CPP}/operators.hpp
    ${WELDER_TEST_COMMON_CPP}/enums.hpp ${WELDER_TEST_COMMON_CPP}/gen_trampolines.hpp
    ${WELDER_TEST_COMMON_CPP}/templates.hpp ${WELDER_TEST_COMMON_CPP}/unions.hpp)
  target_include_directories(${tgt} PRIVATE ${WELDER_TEST_COMMON_CPP})
  # The generated trampoline header (welder::rods::trampolines) is emitted into the
  # parent tests/python build dir; depend on its generation and put it on the path.
  target_include_directories(${tgt} PRIVATE ${WELDER_GEN_TRAMPOLINES_DIR})
  add_dependencies(${tgt} ${WELDER_GEN_TRAMPOLINES_TARGET})
  target_compile_features(${tgt} PRIVATE cxx_std_26)
  target_compile_definitions(${tgt} PRIVATE WELDER_TEST_MODNAME=${tgt})
  target_link_libraries(${tgt} PRIVATE welder::headers welder::pybind11 welder_warnings)
  # pybind11's types carry hidden visibility, so any TU touching them must too —
  # otherwise a default-visibility lambda capturing a `class_`/`module_` trips
  # -Wattributes ("greater visibility than the type of its field"). pybind11_add_module
  # sets this automatically; the CMake-native Python_add_library path (used above)
  # does not, so we set it ourselves, matching pybind11's requirement.
  set_target_properties(${tgt} PROPERTIES
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endfunction()

set(_variants welder_test_pybind11)
welder_add_pybind11_test_module(welder_test_pybind11)

# Negative-compile cases: welder must *reject*, with a hard compile error, any
# attempt to bind a type pybind11 cannot convert to a meaningful Python value.
# Each source is an EXCLUDE_FROM_ALL object library that must FAIL to compile; the
# CTest builds just that target and is marked WILL_FAIL. These need only the
# compiler (no Python runtime), so they are registered before the uv-env gate.
foreach(neg operand_not_welded container_of_unwelded
            virtual_needs_trampoline trampoline_missing_override
            trampoline_ambiguous tack_incomplete_param optin_uninstantiable
            return_policy_dangling alias_forbidden_mark alias_plain_welded
            alias_duplicate nested_excluded_in_signature
            member_alias_duplicate member_alias_forbidden_mark
            member_alias_cross_class alias_optin_in_signature
            nested_virtual_in_specialization
            union_weld_type union_member union_welded_in_namespace
            move_ctor_marked
            property_setter_without_getter property_nonconst_getter
            property_virtual_accessor property_weld_as_on_accessor
            property_shadows_member property_free_function_accessor)
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

# pybind11-stubgen writes the package under here (named after its module,
# welder_test_pybind11/).
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
# generated stubs under that canonical name (rename-safe — the tree uses only
# relative imports) and run the shared cases against them via pytest-mypy-testing.
if(WELDER_BUILD_STUBS AND WELDER_TEST_TYPINGCASES)
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
