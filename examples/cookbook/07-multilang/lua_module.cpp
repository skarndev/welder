// Cookbook 07 — the Lua side, via the sol2 rod. The same annotated header, a
// different rod + name style: snake_case reshapes EVERY name for Lua (classes
// too: Notebook -> notebook). Lua has no runtime docstring slot — the doc /
// returns annotations surface in the generated LuaCATS stub instead (stub_gen.cpp).
#include <welder/vocabulary.hpp>

#include <sol/sol.hpp>
#include <welder/rods/lua/sol2/rod.hpp>

#include "journal.hpp"

// The hand-written loadable-module entry point (the WELDER_MODULE macro would do
// this in one line, but it binds unstyled — spelling luaopen_ out is the way to
// thread a name style in).
extern "C" int luaopen_journal(lua_State* L) {
    sol::state_view lua{L};
    sol::table m{lua.create_table()};
    using weld = welder::welder<welder::rods::sol2::rod, welder::naming::snake_case>;
    weld::weld_module<^^journal>(m);
    return sol::stack::push(L, m);
}