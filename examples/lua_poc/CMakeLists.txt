# Builds the `shapes_lua` loadable Lua module from an annotated namespace via
# WELDER_MODULE. Header-only consumption (welder::headers), so it needs no module
# build; the sol2 backend + Lua headers come through welder::sol2.
#
# welder_sol2_add_module() creates a MODULE library with the bare `.so` name and
# the host-symbol link model Lua's `require` expects (see
# cmake/WelderSol2Module.cmake). The target name must match the namespace token in
# WELDER_MODULE(shapes_lua, ...) so luaopen_shapes_lua is the loaded entry point.
welder_sol2_add_module(shapes_lua example.cpp)
target_compile_features(shapes_lua PRIVATE cxx_std_26)
target_link_libraries(shapes_lua PRIVATE welder::headers)
