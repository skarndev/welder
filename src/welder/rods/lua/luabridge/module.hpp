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

/** @def WELDER_DETAIL_LUAOPEN_EXPORT
    Export decoration for the emitted `luaopen_<ns>` symbol (platform-conditional
    by necessity, like pybind11's/nanobind's own entry-point export macros).

    On Windows any *explicit* `dllexport` elsewhere in the TU — e.g. nanobind's
    headers, when a multi-language header is shared across rods — disables
    MinGW's export-everything default, so the entry symbol must be exported
    explicitly or `require` fails with "the specified procedure could not be
    found". On ELF/Mach-O it pins default visibility, so a TU compiled with
    `-fvisibility=hidden` (common in Python-extension builds) still exposes it. */
#ifndef WELDER_DETAIL_LUAOPEN_EXPORT
#  if defined(_WIN32)
#    define WELDER_DETAIL_LUAOPEN_EXPORT __declspec(dllexport)
#  else
#    define WELDER_DETAIL_LUAOPEN_EXPORT __attribute__((visibility("default")))
#  endif
#endif

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
    @param ns  the namespace / module name token (doubles as the `luaopen_` symbol).
    @param ... optionally, the exact `welder::welder<…>` type to weld with (must be
               over a LuaBridge3-module rod) — see @ref WELDER_MODULE.
*/
#define WELDER_DETAIL_MODULE_ENTRY_luabridge(ns, ...)                             \
    static void welder_glue_##ns##_luabridge(                                     \
        ::welder::rods::luabridge::rod::module_type&);                            \
    extern "C" WELDER_DETAIL_LUAOPEN_EXPORT int luaopen_##ns(                     \
        lua_State* welder_lua_state_) {                                           \
        ::welder::rods::luabridge::rod::module_type welder_module_var_{           \
            welder_lua_state_, {#ns}};                                            \
        using welder_weld_ = ::welder::detail::module_welder_t<                   \
            ::welder::welder<::welder::rods::luabridge::rod>                      \
                __VA_OPT__(, ) __VA_ARGS__>;                                      \
        static_assert(                                                            \
            ::std::is_same_v<typename welder_weld_::module_type,                  \
                             ::welder::rods::luabridge::rod::module_type>,        \
            "WELDER_MODULE(ns, luabridge, W): W must be a welder::welder over a " \
            "rod whose module handle is the LuaBridge3 rod's module_type");       \
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
