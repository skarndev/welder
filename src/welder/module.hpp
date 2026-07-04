#pragma once
/** @file
    Backend-agnostic module entry point — the @ref WELDER_MODULE macro.
*/

/** @def WELDER_MODULE
    Emit a binding module's entry point and bind a namespace into it.

    @param ns      the namespace to bind; its token doubles as the module name.
    @param backend the backend selector (e.g. `pybind11`).

    Defining a binding module needs exactly one irreducible macro. A target
    runtime imports a module by a pasted entry symbol — Python's `PyInit_<name>`,
    Lua's `luaopen_<name>` — and only the preprocessor can form that token; it
    cannot be synthesized from a reflection or a constexpr string. `WELDER_MODULE`
    hides *which* backend macro forms it behind one uniform, backend-selected
    spelling, so the call site is the same across backends.

    Because each language has its own entry symbol, a single shared object can
    expose several languages at once — one `WELDER_MODULE` per backend, each
    emitting its own symbol from the same namespace:

    @code
    WELDER_MODULE(geometry, pybind11) { module.attr("VERSION") = "1.0"; }
    WELDER_MODULE(geometry, lua)      { // lua-specific glue, when available
    }
    @endcode

    (pybind11 and nanobind both emit `PyInit_<name>`, so those two can't coexist
    in one module — pick one Python backend.)

    The namespace token doubles as the module name: `geometry` is both
    `^^geometry` (the bound namespace) and the `PyInit_geometry` /
    `luaopen_geometry` symbol, so there is no separate name argument. The trailing
    `{ }` is optional hand-written glue, run after welder binds the namespace, with
    the backend's module handle named `module` in scope; write `{}` for none.

    @a backend selects the expansion `WELDER_DETAIL_MODULE_ENTRY_<backend>`, which
    the corresponding backend header defines (e.g.
    `<welder/backends/python/pybind11/backend.hpp>`). Selecting a backend whose header you did not
    include is a preprocessor error.
*/
#define WELDER_MODULE(ns, backend) WELDER_DETAIL_MODULE_ENTRY_##backend(ns)
