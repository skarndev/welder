# Builds the `shapes_luabridge` loadable Lua module from an annotated namespace via
# WELDER_MODULE — the LuaBridge3 counterpart of examples/lua_poc (sol2). Header-only
# consumption (welder::headers); the LuaBridge3 backend + Lua headers come through
# welder::luabridge.
#
# welder_luabridge_add_module() creates a MODULE library with the bare `.so` name and
# the host-symbol link model Lua's `require` expects (see
# cmake/WelderLuaBridgeModule.cmake). The target name must match the namespace token
# in WELDER_MODULE(shapes_luabridge, ...) so luaopen_shapes_luabridge is the loaded
# entry point.
welder_luabridge_add_module(shapes_luabridge example.cpp)
target_compile_features(shapes_luabridge PRIVATE cxx_std_26)
target_link_libraries(shapes_luabridge PRIVATE welder::headers)