// Cookbook 07 — the LuaCATS stub generator. Lua drops doc/returns at load time
// (no __doc__ slot), so a build-time generator reflects the SAME welded types and
// writes a ---@meta definition file for the Lua language server — the Lua
// analogue of the Python .pyi.
//
// The WELDER_LUACATS_MAIN macro would emit an UNSTYLED stub; this generator
// spells main() out to pass the same snake_case style the sol2 module binds
// with, so the stub matches the loaded module name-for-name.
#include <fstream>
#include <iostream>

#include <welder/vocabulary.hpp>
#include <welder/rods/lua/luacats/rod.hpp>

#include "journal.hpp"

int main(int argc, char** argv) {
    using stub = welder::rods::luacats::rod;
    if (argc > 1) {
        std::ofstream out{argv[1]};
        stub::generate<^^journal, welder::naming::snake_case>(out);
    } else {
        stub::generate<^^journal, welder::naming::snake_case>(std::cout);
    }
    return 0;
}