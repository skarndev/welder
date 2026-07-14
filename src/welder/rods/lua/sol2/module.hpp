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
    @param ns  the namespace / module name token (doubles as the `luaopen_` symbol).
    @param ... optionally, the exact `welder::welder<…>` type to weld with (must be
               over a sol2-module rod) — see @ref WELDER_MODULE.
*/
#define WELDER_DETAIL_MODULE_ENTRY_sol2(ns, ...)                                  \
    static void welder_glue_##ns##_sol2(::sol::table&);                           \
    extern "C" WELDER_DETAIL_LUAOPEN_EXPORT int luaopen_##ns(                     \
        lua_State* welder_lua_state_) {                                           \
        ::sol::state_view welder_lua_{welder_lua_state_};                         \
        ::sol::table welder_module_var_{welder_lua_.create_table()};             \
        using welder_weld_ = ::welder::detail::module_welder_t<                   \
            ::welder::welder<::welder::rods::sol2::rod>                           \
                __VA_OPT__(, ) __VA_ARGS__>;                                      \
        static_assert(                                                            \
            ::std::is_same_v<typename welder_weld_::module_type, ::sol::table>,   \
            "WELDER_MODULE(ns, sol2, W): W must be a welder::welder over a rod "  \
            "whose module handle is sol::table");                                 \
        welder_weld_::weld_module<^^ns>(                                          \
            welder_module_var_, welder_weld_::noop,                               \
            [](::sol::table& welder_glue_m_) {                                    \
                welder_glue_##ns##_sol2(welder_glue_m_);                          \
            });                                                                   \
        return ::sol::stack::push(welder_lua_state_, welder_module_var_);         \
    }                                                                             \
    static void welder_glue_##ns##_sol2(::sol::table& module)
