#pragma once
/** @file
    Full-automation entry point for the LuaBridge3 Lua rod: the expansion behind
    `WELDER_MODULE(ns, luabridge)`.

    Include this (instead of `rod.hpp`) when you want welder to emit the module's
    `luaopen_` entry symbol too, so no hand-written glue appears in user code:
    @code
    #include <LuaBridge/LuaBridge.h>
    #include <welder/rods/lua/luabridge/module.hpp>
    WELDER_MODULE(mymod, luabridge) {}   // luaopen_mymod + namespace ^^mymod bound
    @endcode
*/
#include <welder/rods/lua/luabridge/rod.hpp>
#include <welder/module.hpp> // the backend-agnostic WELDER_MODULE dispatch

/** @def WELDER_DETAIL_MODULE_ENTRY_luabridge
    LuaBridge3's expansion of the backend-agnostic `WELDER_MODULE(ns, luabridge)`.

    Emits the `luaopen_<ns>` C entry point Lua's `require` calls. LuaBridge3
    registers into named namespaces under the global table, so welder builds the
    module under `_G["<ns>"]`, runs the optional trailing `{ }` block as post-glue
    (the module handle named `module` in scope), then returns that table to Lua and
    clears the `_G` binding (so `require` yields the table without leaving a global
    behind). The block is supplied as the body of a forward-declared, internally-
    linked glue function (the technique the other rods' entry macros use), so both
    `WELDER_MODULE(ns, luabridge) { … }` and `{}` work. Defined at file scope (macros
    ignore namespaces); see `<welder/module.hpp>`.
    @param ns the namespace / module name token (doubles as the `luaopen_` symbol).
*/
#define WELDER_DETAIL_MODULE_ENTRY_luabridge(ns)                                  \
    static void welder_glue_##ns##_luabridge(                                     \
        ::welder::rods::luabridge::rod::module_type&);                            \
    extern "C" int luaopen_##ns(lua_State* welder_lua_state_) {                   \
        ::welder::rods::luabridge::rod::module_type welder_module_var_{           \
            welder_lua_state_, {#ns}};                                            \
        using welder_weld_ = ::welder::welder<::welder::rods::luabridge::rod>;    \
        welder_weld_::weld_module<^^ns>(                                          \
            welder_module_var_, welder_weld_::noop,                               \
            [](::welder::rods::luabridge::rod::module_type& welder_glue_m_) {     \
                welder_glue_##ns##_luabridge(welder_glue_m_);                     \
            });                                                                   \
        lua_getglobal(welder_lua_state_, #ns);          /* the module table */   \
        lua_pushnil(welder_lua_state_);                                          \
        lua_setglobal(welder_lua_state_, #ns);          /* keep _G clean */      \
        return 1;                                                                 \
    }                                                                             \
    static void welder_glue_##ns##_luabridge(                                     \
        ::welder::rods::luabridge::rod::module_type& module)