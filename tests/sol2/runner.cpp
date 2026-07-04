// A minimal Lua host for the sol2 backend tests. The conan `lua` package ships a
// static library but no `lua` interpreter binary, so this is a self-contained
// stand-in: it embeds liblua, points `package.cpath` at the built module
// directory, and runs a Lua script — the welder module's `luaopen_*` resolves its
// `lua_*` symbols back from this host (the runner is linked -export_dynamic).
//
//   welder_lua_runner <script.lua> <module-dir>
//
// Exit code is the script's: any failed assert / error aborts non-zero, which the
// CTest reports as a failure.
#include <lua.hpp>

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <script.lua> <module-dir>\n", argv[0]);
        return 2;
    }
    const char* script{argv[1]};
    const std::string module_dir{argv[2]};

    lua_State* L{luaL_newstate()};
    luaL_openlibs(L);

    // require() looks up C modules on package.cpath; point it at the module dir.
    const std::string cpath{module_dir + "/?.so"};
    lua_getglobal(L, "package");
    lua_pushlstring(L, cpath.c_str(), cpath.size());
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);

    int status{luaL_dofile(L, script)};
    if (status != LUA_OK) {
        std::fprintf(stderr, "lua error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    lua_close(L);
    return 0;
}
