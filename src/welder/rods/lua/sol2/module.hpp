#pragma once
/** @file
    Full-automation entry point for the sol2 Lua rod: the expansion behind
    `WELDER_MODULE(ns, sol2)`.

    Include this (instead of `rod.hpp`) when you want welder to emit the
    module's `luaopen_` entry symbol too, so no hand-written glue appears in user
    code:
    @code
    #include <sol/sol.hpp>
    #include <welder/rods/lua/sol2/module.hpp>
    WELDER_MODULE(mymod, sol2) {}   // luaopen_mymod + namespace ^^mymod bound
    @endcode
*/
#include <welder/rods/lua/sol2/rod.hpp>
#include <welder/module.hpp> // the backend-agnostic WELDER_MODULE dispatch

/** @def WELDER_DETAIL_MODULE_ENTRY_sol2
    sol2's expansion of the backend-agnostic `WELDER_MODULE(ns, sol2)`.

    Emits the `luaopen_<ns>` C entry point Lua's `require` calls: it views the
    borrowed `lua_State*` with a `sol::state_view`, creates the module table, welds
    namespace `^^ns` into it, runs the optional trailing `{ }` block as post-glue
    (with the module table named `module` in scope), and returns the table to Lua.
    The block is supplied as the body of a forward-declared, internally-linked glue
    function (the technique the Python backends' entry macros use), so both
    `WELDER_MODULE(ns, sol2) { … }` and `WELDER_MODULE(ns, sol2) {}` work. Defined at
    file scope (macros ignore namespaces); see `<welder/module.hpp>`.
    @param ns the namespace / module name token (doubles as the `luaopen_` symbol).
*/
#define WELDER_DETAIL_MODULE_ENTRY_sol2(ns)                                       \
    static void welder_glue_##ns##_sol2(::sol::table&);                           \
    extern "C" int luaopen_##ns(lua_State* welder_lua_state_) {                   \
        ::sol::state_view welder_lua_{welder_lua_state_};                         \
        ::sol::table welder_module_var_{welder_lua_.create_table()};             \
        using welder_weld_ = ::welder::welder<::welder::rods::sol2::rod>;          \
        welder_weld_::weld_module<^^ns>(                                          \
            welder_module_var_, welder_weld_::noop,                               \
            [](::sol::table& welder_glue_m_) {                                    \
                welder_glue_##ns##_sol2(welder_glue_m_);                          \
            });                                                                   \
        return ::sol::stack::push(welder_lua_state_, welder_module_var_);         \
    }                                                                             \
    static void welder_glue_##ns##_sol2(::sol::table& module)
