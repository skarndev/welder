#pragma once
/** @file
    Full-automation entry point for the pybind11 rod: the expansion behind
    `WELDER_MODULE(ns, pybind11)`.

    Include this (instead of `rod.hpp`) when you want welder to emit the
    module's C entry symbol too, so no pybind11 macro appears in user code:
    @code
    #include <pybind11/pybind11.h>
    #include <welder/rods/python/pybind11/module.hpp>
    WELDER_MODULE(mymod, pybind11) {}   // PyInit_mymod + namespace ^^mymod bound
    @endcode
*/
#include <welder/rods/python/pybind11/rod.hpp>
#include <welder/module.hpp> // the backend-agnostic WELDER_MODULE dispatch

/** @def WELDER_DETAIL_MODULE_ENTRY_pybind11
    pybind11's expansion of the backend-agnostic `WELDER_MODULE(ns, pybind11)`.

    Emits the `PyInit_<ns>` entry point, welds namespace `^^ns` into it, then runs
    the optional trailing `{ }` block as post-glue with the module handle named
    `module` in scope. The block is supplied as the body of a forward-declared,
    internally-linked glue function (the same technique `PYBIND11_MODULE` itself
    uses for its body), so `WELDER_MODULE(ns, pybind11) { … }` and
    `WELDER_MODULE(ns, pybind11) {}` both work. Defined at file scope (macros ignore
    namespaces); see `<welder/module.hpp>` for the `WELDER_MODULE` dispatch.
    @param ns  the namespace / module name token.
    @param ... optionally, the exact `welder::welder<…>` type to weld with (must be
               over a pybind11-module rod) — see @ref WELDER_MODULE.
*/
#define WELDER_DETAIL_MODULE_ENTRY_pybind11(ns, ...)                              \
    static void welder_glue_##ns##_pybind11(::pybind11::module_&);                \
    PYBIND11_MODULE(ns, welder_module_var_) {                                     \
        using welder_weld_ = ::welder::detail::module_welder_t<                   \
            ::welder::welder<::welder::rods::pybind11::rod<>>                     \
                __VA_OPT__(, ) __VA_ARGS__>;                                      \
        static_assert(                                                            \
            ::std::is_same_v<typename welder_weld_::module_type,                  \
                             ::pybind11::module_>,                                \
            "WELDER_MODULE(ns, pybind11, W): W must be a welder::welder over a "  \
            "rod whose module handle is pybind11::module_");                      \
        welder_weld_::weld_module<^^ns>(                                          \
            welder_module_var_, welder_weld_::noop,                               \
            [](::pybind11::module_& welder_glue_m_) {                             \
                welder_glue_##ns##_pybind11(welder_glue_m_);                      \
            });                                                                   \
    }                                                                             \
    static void welder_glue_##ns##_pybind11(::pybind11::module_& module)
