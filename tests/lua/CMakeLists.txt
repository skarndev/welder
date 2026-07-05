# sol2 (Lua) backend tests — the Lua counterpart of tests/python. Build the test
# module from the *shared* C++ case tree (tests/common/cpp), the same cases the
# pybind11/nanobind extensions use, then run the busted specs (spec/*_spec.lua)
# against it under the user's Lua interpreter.
#
# Unlike the Python trees there is no module-form variant (a Lua binding TU is
# header-only; see cmake/WelderSol2Module.cmake). The test framework — busted — is
# installed with luarocks into a build-local tree (the analogue of uv/pytest on the
# Python side): nothing is vendored. Both the module ABI and the interpreter come
# from the *same* user Lua (WELDER_LUA_DIR / WELDER_LUA_VERSION), found by FindLua
# in src/welder/backends/CMakeLists.txt — welder::sol2 carries its headers.
find_package(sol2 REQUIRED)

# The loadable module. Its name doubles as the luaopen_ symbol / require() name.
welder_sol2_add_module(welder_test_sol2
  cpp/bindings.cpp
  ${WELDER_TEST_COMMON_CPP}/resolution.hpp ${WELDER_TEST_COMMON_CPP}/methods.hpp
  ${WELDER_TEST_COMMON_CPP}/inheritance.hpp ${WELDER_TEST_COMMON_CPP}/namespace.hpp
  ${WELDER_TEST_COMMON_CPP}/operators.hpp ${WELDER_TEST_COMMON_CPP}/enums.hpp)
target_include_directories(welder_test_sol2 PRIVATE ${WELDER_TEST_COMMON_CPP})
target_compile_features(welder_test_sol2 PRIVATE cxx_std_26)
target_link_libraries(welder_test_sol2 PRIVATE welder::headers)

# Behavioral specs: run the busted suite (the analogue of the uv gate on the Python
# side). busted is a multi-module rock, so — like pytest under uv — we install it
# rather than vendor it: into a build-local luarocks tree, at configure time, for
# the SAME Lua the module is built against. Its `bin/busted` wrapper bakes in that
# interpreter and its own dependency paths, so the CTest just runs the wrapper;
# LUA_PATH finds our spec helper (helper.lua) and LUA_CPATH the freshly built .so.
#
# --no-auto-insulate: busted otherwise snapshots/restores globals per spec file,
# which unloads and re-`require`s our C module each time — re-running luaopen and
# re-registering sol2 usertypes on the same lua_State, which corrupts them. One
# shared module instance across all specs is exactly what we want (the Lua analogue
# of the pytest conftest fixture handing every test the same imported extension).
#
# Two things must line up for the test to register: a `luarocks` (found, or set
# -DWELDER_LUAROCKS_EXECUTABLE), and a concrete Lua install prefix (WELDER_LUA_DIR)
# for luarocks to target — a bare interpreter version is not enough. Missing either
# only skips the behavioral test; the module above still *builds* (a compile check).
find_program(WELDER_LUAROCKS_EXECUTABLE NAMES luarocks)
set(WELDER_BUSTED_VERSION "2.3.0" CACHE STRING "busted rock version for the Lua tests")

if(NOT WELDER_LUAROCKS_EXECUTABLE)
  message(WARNING "welder: 'luarocks' not found — the Lua behavioral test "
    "(luatest.sol2) is skipped. Install it, e.g. `brew install luarocks`.")
elseif(NOT WELDER_LUA_DIR)
  message(WARNING "welder: WELDER_LUA_DIR is unset — luarocks needs a concrete Lua "
    "install prefix, so the Lua behavioral test (luatest.sol2) is skipped. Set "
    "-DWELDER_LUA_DIR=`brew --prefix lua@${WELDER_LUA_VERSION}`.")
else()
  # Provision busted once into a build-local tree (mirrors the `uv sync` step).
  set(_lua_tree ${CMAKE_CURRENT_BINARY_DIR}/lua_modules)
  message(STATUS "welder: installing busted (Lua ${WELDER_LUA_VERSION}) via luarocks...")
  execute_process(
    COMMAND ${WELDER_LUAROCKS_EXECUTABLE}
            --tree ${_lua_tree} --lua-version ${WELDER_LUA_VERSION}
            --lua-dir ${WELDER_LUA_DIR}
            install busted ${WELDER_BUSTED_VERSION}
    RESULT_VARIABLE _lr_res
    OUTPUT_VARIABLE _lr_out ERROR_VARIABLE _lr_out)
  if(NOT _lr_res EQUAL 0)
    message(WARNING "welder: 'luarocks install busted' failed; the Lua behavioral "
      "test (luatest.sol2) is skipped:\n${_lr_out}")
  else()
    add_test(
      NAME luatest.sol2
      COMMAND ${_lua_tree}/bin/busted --verbose --no-auto-insulate
              ${CMAKE_CURRENT_SOURCE_DIR}/spec)
    # The `\;\;` become a literal `;;` (append Lua's default path); the unescaped
    # `;` separates the two ENVIRONMENT entries.
    set_tests_properties(luatest.sol2 PROPERTIES ENVIRONMENT
      "LUA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/?.lua\;\;;LUA_CPATH=$<TARGET_FILE_DIR:welder_test_sol2>/?.so\;\;")
  endif()
endif()

# Negative-compile: welder must reject, with a hard compile error, a member whose
# type sol2 cannot represent unless welder registers it and it is not welded.
add_library(welder_neg_sol2_unwelded OBJECT EXCLUDE_FROM_ALL cpp/neg/field_unwelded.cpp)
target_include_directories(welder_neg_sol2_unwelded PRIVATE ${WELDER_TEST_COMMON_CPP})
target_compile_features(welder_neg_sol2_unwelded PRIVATE cxx_std_26)
target_link_libraries(welder_neg_sol2_unwelded PRIVATE welder::sol2)
set_target_properties(welder_neg_sol2_unwelded PROPERTIES CXX_SCAN_FOR_MODULES OFF)
add_test(NAME negcompile.sol2_unwelded
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_neg_sol2_unwelded --config $<CONFIG>)
set_tests_properties(negcompile.sol2_unwelded PROPERTIES WILL_FAIL TRUE)
