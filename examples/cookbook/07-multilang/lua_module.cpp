// Cookbook 07 — the Lua side, via the sol2 rod. The same annotated header, a
// different rod + name style: snake_case reshapes EVERY name for Lua (classes
// too: Notebook -> notebook). Lua has no runtime docstring slot — the doc /
// returns annotations surface in the generated LuaCATS stub instead (stub_gen.cpp).
//
// Compare lua_module_luabridge.cpp: one language, two frameworks — only the rod
// selector differs.
#include <welder/vocabulary.hpp>

#include <sol/sol.hpp>
#include <welder/rods/lua/sol2/module.hpp>

#include "journal.hpp"

WELDER_MODULE(journal, sol2,
              welder::welder<welder::rods::sol2::rod,
                             welder::naming::snake_case>) {}