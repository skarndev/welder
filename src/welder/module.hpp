#pragma once
/** @file
    Rod-agnostic module entry point — the @ref WELDER_MODULE macro.
*/

namespace welder::inline v0 {
namespace detail {

/** Pick the entry macro's welder: the user's optional override, else the rod's
    default `welder::welder<rod>`.

    The type-level half of `WELDER_MODULE`'s optional third argument: each rod's
    entry macro pastes its `__VA_ARGS__` (the user's welder type — commas inside
    its template-id survive, since they are re-pasted into a template argument
    list) after the rod's default, and this alias resolves to the override when
    one was given. */
template <class Default, class... Override>
struct module_welder {
    using type = Default;
};
template <class Default, class First, class... Rest>
struct module_welder<Default, First, Rest...> {
    using type = First;
};
template <class Default, class... Override>
using module_welder_t = typename module_welder<Default, Override...>::type;

} // namespace detail
} // namespace welder

/** @def WELDER_MODULE
    Emit a binding module's entry point and weld a namespace into it.

    @param ns  the namespace to bind; its token doubles as the module name.
    @param rod the rod selector (e.g. `pybind11`).
    @param ... optionally, the exact `welder::welder<…>` type to drive the weld
               with — the way to thread a name style (or a custom carriage)
               through the one-line module form. Defaults to the rod's plain
               `welder::welder<rod>`. Commas inside the template-id are fine (the
               macro is variadic), and the type's `module_type` must match the
               selected rod's (checked by a `static_assert` in the expansion):
    @code
    WELDER_MODULE(geometry, pybind11,
                  welder::welder<welder::rods::pybind11::rod<>,
                                 welder::rods::python::pep8>) {}
    @endcode

    Defining a binding module needs exactly one irreducible macro. A target
    runtime imports a module by a pasted entry symbol — Python's `PyInit_<name>`,
    Lua's `luaopen_<name>` — and only the preprocessor can form that token; it
    cannot be synthesized from a reflection or a constexpr string. `WELDER_MODULE`
    hides *which* rod's macro forms it behind one uniform, rod-selected spelling,
    so the call site is the same across rods.

    Because each language has its own entry symbol, a single shared object can
    expose several languages at once — one `WELDER_MODULE` per rod, each emitting
    its own symbol from the same namespace:

    @code
    WELDER_MODULE(geometry, pybind11) { module.attr("VERSION") = "1.0"; }
    WELDER_MODULE(geometry, sol2)     { module["VERSION"] = "1.0"; } // Lua glue
    @endcode

    (pybind11 and nanobind both emit `PyInit_<name>`, so those two can't coexist
    in one module — pick one Python rod.)

    The namespace token doubles as the module name: `geometry` is both
    `^^geometry` (the bound namespace) and the `PyInit_geometry` /
    `luaopen_geometry` symbol, so there is no separate name argument. The trailing
    `{ }` is optional hand-written glue, run after welder binds the namespace, with
    the rod's module handle named `module` in scope; write `{}` for none.

    @a rod selects the expansion `WELDER_DETAIL_MODULE_ENTRY_<rod>`, which the
    corresponding rod's `module.hpp` defines (e.g.
    `<welder/rods/python/pybind11/module.hpp>`). Selecting a rod whose `module.hpp`
    you did not include is a preprocessor error.
*/
#define WELDER_MODULE(ns, rod, ...)                                               \
    WELDER_DETAIL_MODULE_ENTRY_##rod(ns __VA_OPT__(, ) __VA_ARGS__)
