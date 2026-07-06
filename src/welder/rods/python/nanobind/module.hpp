#pragma once
/** @file
    Full-automation entry point for the nanobind rod: the expansion behind
    `WELDER_MODULE(ns, nanobind)`.

    Include this (instead of `rod.hpp`) when you want welder to emit the
    module's C entry symbol too, so no nanobind macro appears in user code:
    @code
    #include <nanobind/nanobind.h>
    #include <welder/rods/python/nanobind/module.hpp>
    WELDER_MODULE(mymod, nanobind) {}   // PyInit_mymod + namespace ^^mymod bound
    @endcode
*/
#include <welder/rods/python/nanobind/rod.hpp>
#include <welder/module.hpp> // the backend-agnostic WELDER_MODULE dispatch

/** @def WELDER_DETAIL_MODULE_ENTRY_nanobind
    nanobind's expansion of the backend-agnostic `WELDER_MODULE(ns, nanobind)`.

    Emits nanobind's module entry point, welds namespace `^^ns` into it, then runs
    the optional trailing `{ }` block as post-glue with the module handle named
    `module` in scope. The block is supplied as the body of a forward-declared,
    internally-linked glue function (the same technique nanobind's `NB_MODULE`
    itself uses for its body), so `WELDER_MODULE(ns, nanobind) { … }` and
    `WELDER_MODULE(ns, nanobind) {}` both work. Defined at file scope (macros ignore
    namespaces); see `<welder/module.hpp>` for the `WELDER_MODULE` dispatch.
    @param ns the namespace / module name token.
*/
#define WELDER_DETAIL_MODULE_ENTRY_nanobind(ns)                                   \
    static void welder_glue_##ns##_nanobind(::nanobind::module_&);                \
    NB_MODULE(ns, welder_module_var_) {                                           \
        using welder_weld_ = ::welder::welder<::welder::rods::nanobind::rod>;      \
        welder_weld_::weld_module<^^ns>(                                          \
            welder_module_var_, welder_weld_::noop,                               \
            [](::nanobind::module_& welder_glue_m_) {                             \
                welder_glue_##ns##_nanobind(welder_glue_m_);                      \
            });                                                                   \
    }                                                                             \
    static void welder_glue_##ns##_nanobind(::nanobind::module_& module)
