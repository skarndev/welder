# Each example may emit .pyi stubs next to its module via
# welder_pybind11_generate_stubs() (called in its own CMakeLists, since POST_BUILD
# must attach to a target in the same directory). It's opt-in: examples build
# against the system Python (find_package(Python)), which has no pybind11-stubgen,
# so pass -DWELDER_STUBGEN_PYTHON=<interp> pointing at an ABI-compatible
# interpreter that has it installed (e.g. the tests/ uv env) to turn it on.
if(WELDER_BUILD_PYBIND11)
  if(WELDER_BUILD_MODULE)
    add_subdirectory(python_poc) # consumes `import welder;`
  endif()
  add_subdirectory(python_poc_headeronly) # consumes welder header-only
  add_subdirectory(welder_module)         # whole-module binding via WELDER_MODULE
endif()

if(WELDER_BUILD_SOL2)
  add_subdirectory(lua_poc) # whole-module Lua binding via WELDER_MODULE(_, sol2)
endif()

if(WELDER_BUILD_LUABRIDGE)
  add_subdirectory(luabridge_poc) # whole-module Lua binding via WELDER_MODULE(_, luabridge)
endif()
