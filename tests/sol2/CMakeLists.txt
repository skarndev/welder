# sol2 (Lua) backend tests. Build the test module from the *shared* C++ case tree
# (tests/common/cpp) — the same cases the pybind11/nanobind extensions use — then
# run the Lua assertions (test.lua) against it through a small embedded-Lua runner.
# Unlike the Python backends there is no module-form variant (a Lua binding TU is
# header-only; see cmake/WelderSol2Module.cmake) and no uv/pytest dependency.
find_package(sol2 REQUIRED)
find_package(lua REQUIRED)

# The loadable module. Its name doubles as the luaopen_ symbol / require() name.
welder_sol2_add_module(welder_test_sol2
  cpp/bindings.cpp
  ${WELDER_TEST_COMMON_CPP}/resolution.hpp ${WELDER_TEST_COMMON_CPP}/methods.hpp
  ${WELDER_TEST_COMMON_CPP}/inheritance.hpp ${WELDER_TEST_COMMON_CPP}/namespace.hpp
  ${WELDER_TEST_COMMON_CPP}/operators.hpp ${WELDER_TEST_COMMON_CPP}/enums.hpp)
target_include_directories(welder_test_sol2 PRIVATE ${WELDER_TEST_COMMON_CPP})
target_compile_features(welder_test_sol2 PRIVATE cxx_std_26)
target_link_libraries(welder_test_sol2 PRIVATE welder::headers)

# The embedded-Lua host that loads and drives the module (the conan lua package
# has no interpreter binary). It links liblua and exports its symbols so the
# dynamically-loaded module resolves lua_* back from here.
add_executable(welder_lua_runner runner.cpp)
target_link_libraries(welder_lua_runner PRIVATE lua::lua)
# Same reason as the sol2 modules: <luaconf.h> can't see LLONG_MAX under p1689
# module scanning; the runner uses no imports, so skip the scan.
set_target_properties(welder_lua_runner PROPERTIES CXX_SCAN_FOR_MODULES OFF)
if(APPLE)
  target_link_options(welder_lua_runner PRIVATE -Wl,-export_dynamic)
else()
  target_link_options(welder_lua_runner PRIVATE -rdynamic)
endif()

# Behavioral assertions: run test.lua with package.cpath pointing at the module.
add_test(
  NAME luatest.sol2
  COMMAND welder_lua_runner ${CMAKE_CURRENT_SOURCE_DIR}/test.lua
          $<TARGET_FILE_DIR:welder_test_sol2>)

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
