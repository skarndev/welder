# LuaCATS stub backend test (backend-agnostic; needs only the compiler).
#
# The stub backend reflects welded Lua types and emits a LuaCATS `---@meta`
# definition file — the Lua analogue of the Python `.pyi` stubs, carrying the
# docstrings the sol2 runtime backend drops. It depends on neither sol2 nor a Lua
# runtime (pure reflection + text), so this lives at the top of tests/ (like the
# backend_probe / cleandoc compile checks), NOT under the sol2-gated lua/ tree.
#
# We dogfood the real user path — welder_luacats_generate_stub() builds a generator
# executable from the doc-rich case (cpp/stub_gen.cpp) and runs it to produce the
# stub as an ALL target — then golden-compare. lua-language-server is not assumed
# present (a validate-if-present hook is future work), so the byte-exact golden
# (stub.golden.lua) is the gate, the same shape as the doxyfilter goldens.
welder_luacats_generate_stub(welder_test_luacats
  SOURCES cpp/stub_gen.cpp
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/stubdemo.lua
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cpp/stub_gen.cpp)

# The generated stub (built by the ALL target above) must match the checked-in
# golden byte-for-byte (ignoring line-ending style for portability).
add_test(NAME luacats.stub_golden
  COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol
          ${CMAKE_CURRENT_BINARY_DIR}/stubdemo.lua
          ${CMAKE_CURRENT_SOURCE_DIR}/stub.golden.lua)
