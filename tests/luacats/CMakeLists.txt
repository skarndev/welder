# LuaCATS stub backend test (backend-agnostic; needs only the compiler to build,
# plus an optional lua-language-server to lint).
#
# The stub backend reflects welded Lua types and emits a LuaCATS `---@meta`
# definition file — the Lua analogue of the Python `.pyi` stubs, carrying the
# docstrings the sol2 runtime backend drops. It depends on neither sol2 nor a Lua
# runtime (pure reflection + text), so this lives at the top of tests/ (like the
# rod_probe / cleandoc compile checks), NOT under the sol2-gated lua/ tree.
#
# We dogfood the real user path — welder_luacats_generate_stub() builds a generator
# executable from the doc-rich case (cpp/stub_gen.cpp) and runs it to produce the
# stub as an ALL target. The stub is emitted into an isolated workspace dir so the
# language server (below) can treat it as a one-file workspace.
set(_stub_ws ${CMAKE_CURRENT_BINARY_DIR}/stub)
welder_luacats_generate_stub(welder_test_luacats
  SOURCES cpp/stub_gen.cpp
  OUTPUT  ${_stub_ws}/stubdemo.lua
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cpp/stub_gen.cpp)

# The generated stub (built by the ALL target above) must match the checked-in
# golden byte-for-byte (ignoring line-ending style for portability).
add_test(NAME luacats.stub_golden
  COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol
          ${_stub_ws}/stubdemo.lua
          ${CMAKE_CURRENT_SOURCE_DIR}/stub.golden.lua)

# ...and — when a lua-language-server is present — *lint* the generated stub, the
# Lua analogue of the Python `stubcheck.<variant>` mypy gate. Unlike mypy over the
# `.pyi`, this proves the emitted LuaCATS is well-formed and internally consistent:
# every `---@type`/`---@field` type resolves, `---@class` bases exist, and every
# `---@operator` name is one lua-language-server actually models. It caught the
# original `---@operator eq/lt/le/[]` emissions (comparison/subscript metamethods
# have no LuaCATS `@operator` spelling; they bind at runtime but can't be typed).
#
# The gate is optional (like the uv/luarocks gates): a missing lua-language-server
# only skips the lint (the golden still gates the output). `--check` reports via its
# exit code (0 = clean, non-zero = diagnostics), just like mypy. The strictness that
# matters for a `---@meta` file — which *defines* but never *uses* its types — comes
# from .luarc.json's `neededFileStatus: Any!`, so it must sit in the checked
# workspace; copy it next to the stub. (A single-file `--check` ignores it.)
find_program(WELDER_LUA_LANGUAGE_SERVER NAMES lua-language-server)
if(WELDER_LUA_LANGUAGE_SERVER)
  configure_file(.luarc.json ${_stub_ws}/.luarc.json COPYONLY)
  add_test(NAME stubcheck.luacats
    COMMAND ${WELDER_LUA_LANGUAGE_SERVER}
            --check ${_stub_ws}
            --checklevel=Warning
            --configpath ${_stub_ws}/.luarc.json
            --logpath ${CMAKE_CURRENT_BINARY_DIR}/lls-log)
else()
  message(STATUS
    "welder: lua-language-server not found; skipping the LuaCATS stubcheck "
    "(set WELDER_LUA_LANGUAGE_SERVER to override). The stub golden still runs.")
endif()
