# Lua backend tests — the Lua counterpart of tests/python. Build a test module from
# the *shared* C++ case tree (tests/common/cpp) for each enabled Lua rod (sol2,
# LuaBridge3), then run the *same* busted specs (spec/*_spec.lua) against each: a
# cross-backend consistency check. The specs pick their module at run time via the
# WELDER_TEST_LUA_MODULE env var (see helper.lua), so one spec suite serves both.
#
# A Lua binding TU is header-only (as is every welder TU today). The test
# framework — busted — is installed with luarocks into a
# build-local tree (the analogue of uv/pytest): nothing is vendored. Each rod's
# module ABI and its interpreter come from the *same* user Lua (sol2:
# WELDER_LUA_DIR/_VERSION; LuaBridge3: WELDER_LUABRIDGE_LUA_DIR/_VERSION), so when the
# two rods target the same Lua minor they share one busted tree; a differing minor
# gets its own.

set(WELDER_LUA_CASE_HEADERS
  ${WELDER_TEST_COMMON_CPP}/resolution.hpp ${WELDER_TEST_COMMON_CPP}/methods.hpp
  ${WELDER_TEST_COMMON_CPP}/inheritance.hpp ${WELDER_TEST_COMMON_CPP}/namespace.hpp
  ${WELDER_TEST_COMMON_CPP}/operators.hpp ${WELDER_TEST_COMMON_CPP}/enums.hpp
  ${WELDER_TEST_COMMON_CPP}/naming.hpp)

find_program(WELDER_LUAROCKS_EXECUTABLE NAMES luarocks)
set(WELDER_BUSTED_VERSION "2.3.0" CACHE STRING "busted rock version for the Lua tests")

# Install busted for a given Lua (version,dir) into a per-version build-local tree
# (the analogue of `uv sync`), reusing it if already present. Sets <wrapper_out> to
# the `bin/busted` wrapper (which bakes in the matched interpreter), or empty when it
# cannot be provisioned (missing luarocks / dir / install failure) — in which case
# the caller only skips the behavioral CTest; the module still builds as a compile
# check. Needs both a `luarocks` and a concrete Lua install prefix (a bare version is
# not enough for luarocks to target).
function(_welder_lua_install_busted version dir wrapper_out)
  set(${wrapper_out} "" PARENT_SCOPE)
  if(NOT WELDER_LUAROCKS_EXECUTABLE)
    message(WARNING "welder: 'luarocks' not found — Lua behavioral tests are skipped. "
      "Install it, e.g. `brew install luarocks`.")
    return()
  endif()
  if(NOT dir)
    message(WARNING "welder: no Lua install prefix for Lua ${version} — luarocks needs "
      "one, so the matching Lua behavioral test is skipped.")
    return()
  endif()
  set(tree ${CMAKE_CURRENT_BINARY_DIR}/lua_modules_${version})
  if(NOT EXISTS ${tree}/bin/busted)
    message(STATUS "welder: installing busted (Lua ${version}) via luarocks...")
    execute_process(
      COMMAND ${WELDER_LUAROCKS_EXECUTABLE}
              --tree ${tree} --lua-version ${version} --lua-dir ${dir}
              install busted ${WELDER_BUSTED_VERSION}
      RESULT_VARIABLE _res OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
    if(NOT _res EQUAL 0)
      message(WARNING "welder: 'luarocks install busted' (Lua ${version}) failed; the "
        "matching Lua behavioral test is skipped:\n${_out}")
      return()
    endif()
  endif()
  set(${wrapper_out} ${tree}/bin/busted PARENT_SCOPE)
endfunction()

# Register the busted CTest for one rod's built module against the shared spec suite.
# --no-auto-insulate: busted otherwise snapshots/restores globals per spec file,
# unloading and re-`require`ing our C module each time (re-running luaopen and
# corrupting the rod's registrations); one shared instance across all specs is what
# we want (the Lua analogue of the pytest conftest fixture). LUA_PATH finds the spec
# helper, LUA_CPATH the freshly built .so, WELDER_TEST_LUA_MODULE selects it.
function(_welder_lua_add_test test_name module_name wrapper)
  if(NOT wrapper)
    return()
  endif()
  add_test(NAME ${test_name}
    COMMAND ${wrapper} --verbose --no-auto-insulate ${CMAKE_CURRENT_SOURCE_DIR}/spec)
  # The `\;\;` become a literal `;;` (append Lua's default path); the unescaped `;`
  # separates the ENVIRONMENT entries.
  set_tests_properties(${test_name} PROPERTIES ENVIRONMENT
    "LUA_PATH=${CMAKE_CURRENT_SOURCE_DIR}/?.lua\;\;;LUA_CPATH=$<TARGET_FILE_DIR:${module_name}>/?.so\;\;;WELDER_TEST_LUA_MODULE=${module_name}")
endfunction()

# --- sol2 ----------------------------------------------------------------------
if(WELDER_BUILD_SOL2)
  find_package(sol2 REQUIRED)
  welder_sol2_add_module(welder_test_sol2 cpp/bindings.cpp ${WELDER_LUA_CASE_HEADERS})
  target_include_directories(welder_test_sol2 PRIVATE ${WELDER_TEST_COMMON_CPP})
  target_compile_features(welder_test_sol2 PRIVATE cxx_std_26)
  target_link_libraries(welder_test_sol2 PRIVATE welder::headers)

  _welder_lua_install_busted(${WELDER_LUA_VERSION} "${WELDER_LUA_DIR}" _sol2_busted)
  _welder_lua_add_test(luatest.sol2 welder_test_sol2 "${_sol2_busted}")

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
endif()

# --- LuaBridge3 ----------------------------------------------------------------
if(WELDER_BUILD_LUABRIDGE AND TARGET welder::luabridge)
  welder_luabridge_add_module(welder_test_luabridge
    cpp/bindings_luabridge.cpp ${WELDER_LUA_CASE_HEADERS})
  target_include_directories(welder_test_luabridge PRIVATE ${WELDER_TEST_COMMON_CPP})
  target_compile_features(welder_test_luabridge PRIVATE cxx_std_26)
  target_link_libraries(welder_test_luabridge PRIVATE welder::headers)

  _welder_lua_install_busted(${WELDER_LUABRIDGE_LUA_VERSION} "${WELDER_LUABRIDGE_LUA_DIR}"
    _lb_busted)
  _welder_lua_add_test(luatest.luabridge welder_test_luabridge "${_lb_busted}")

  # Negative-compile: same gate, through the LuaBridge3 rod.
  add_library(welder_neg_luabridge_unwelded OBJECT EXCLUDE_FROM_ALL
    cpp/neg/field_unwelded_luabridge.cpp)
  target_include_directories(welder_neg_luabridge_unwelded PRIVATE ${WELDER_TEST_COMMON_CPP})
  target_compile_features(welder_neg_luabridge_unwelded PRIVATE cxx_std_26)
  target_link_libraries(welder_neg_luabridge_unwelded PRIVATE welder::luabridge)
  set_target_properties(welder_neg_luabridge_unwelded PROPERTIES CXX_SCAN_FOR_MODULES OFF)
  add_test(NAME negcompile.luabridge_unwelded
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target welder_neg_luabridge_unwelded --config $<CONFIG>)
  set_tests_properties(negcompile.luabridge_unwelded PROPERTIES WILL_FAIL TRUE)
endif()