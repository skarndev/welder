#pragma once
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

#include <welder/rods/python/trampoline.hpp> // virtual_slot_count, trampoline_for, …

#include <pybind11/pybind11.h>

/** @file
    pybind11's half of welder's virtual-override support: the reflection-driven
    dispatch helper that replaces `PYBIND11_OVERRIDE` / `PYBIND11_OVERRIDE_PURE`, and
    the `WELDER_PY_TRAMPOLINE` / `WELDER_PY_OVERRIDE` authoring macros.

    The backend-neutral machinery (slot count, the `trampoline_for` registration
    hook, the coverage check) lives in `<welder/rods/python/trampoline.hpp>`. This
    header only adds what is specific to pybind11: `get_override` lookup and
    `cast_safe`. Unlike nanobind, pybind11 keeps no per-instance trampoline storage —
    the override is found from the C++ `this` pointer — so `WELDER_PY_TRAMPOLINE`
    injects only the base alias and inherited constructors. The macros are spelled the
    same as nanobind's (a translation unit includes exactly one Python rod), so a
    single trampoline definition compiles under either backend.

    Include this from a pybind11 binding translation unit *before* the header that
    defines your trampoline subclass.
*/

namespace welder::inline v0::rods::pybind11 {

// The unqualified name `pybind11` resolves to *this* namespace inside it; alias the
// real library the way pybind11's own docs do (see rod.hpp).
namespace py = ::pybind11;

/** Dispatch a captured virtual call to its Python override, else to the C++ base.

    The reflection-driven replacement for `PYBIND11_OVERRIDE`/`PYBIND11_OVERRIDE_PURE`:
    the method name (for the Python lookup), the return type, and whether the method is
    pure are all read from @a Fn — a reflection of the *base* virtual — rather than
    spelled by hand.

    @a self is the trampoline instance viewed as the *welded* type it is bound under
    (`welder_py_base`), **not** the virtual's declaring class. pybind11's
    `get_override(const T*, name)` keys the Python-object lookup off `typeid(T)` — the
    static pointer type — so `T` must be the type the instance is *registered* as
    (`class_<T, Trampoline, …>`). For a virtual @a type inherits from a base, the
    declaring class differs from the registered type; casting to the declaring class
    would look the instance up under the wrong registration and miss the override (C++
    would silently see the base implementation). The caller (`WELDER_PY_OVERRIDE`) casts
    `*this` to `welder_py_base` for exactly this reason.

    The base-class fallback is passed in as @a base_call rather than derived from
    @a Fn: splicing a member function into a member access (`self.[:Fn:]()`) performs
    a *virtual* call, which would re-enter the override and recurse. The caller
    supplies a lambda doing a textually *qualified* `Base::method(...)` call, which is
    non-virtual — matching what `PYBIND11_OVERRIDE`'s `cname::fn(...)` tail does.

    @tparam Fn a reflection of the base virtual member function.
    @param self      the trampoline instance as its registered welded type (for the
                     `get_override` lookup).
    @param base_call an invocable performing the qualified base-class call.
    @param args      the forwarded call arguments. */
template <auto Fn, typename Self, typename BaseCall, typename... Args>
decltype(auto) override_dispatch(const Self& self, BaseCall&& base_call,
                                 Args&&... args) {
    using ret_type = [:std::meta::return_type_of(Fn):];
    static_assert(
        !std::is_reference_v<ret_type>,
        "welder: cannot trampoline a virtual method that returns a reference "
        "(pybind11 cannot keep the referent alive across the C++/Python boundary); "
        "return by value, or mark the type [[=welder::rods::python::bind_flat]].");

    constexpr const char* name{
        std::define_static_string(std::meta::identifier_of(Fn))};

    py::gil_scoped_acquire gil;
    py::function override{py::get_override(&self, name)};
    if (override)
        return py::detail::cast_safe<ret_type>(
            override(std::forward<Args>(args)...));

    if constexpr (std::meta::is_pure_virtual(Fn))
        // No base to fall back to and no Python override: pybind11's own diagnostic.
        py::pybind11_fail(
            "welder: tried to call a pure virtual method with no Python override");
    else
        return std::forward<BaseCall>(base_call)(std::forward<Args>(args)...);
}

} // namespace welder::rods::pybind11

/** Declare class @a TRAMP a pybind11 trampoline for @a BASE.

    Place at the top of a trampoline subclass body. Introduces the `welder_py_base`
    alias the override macro keys off, inherits @a BASE's constructors, and — the
    reason the macro takes the trampoline's own name — declares the two
    constructors inherited-constructor syntax cannot provide: a defaulted default
    constructor (any user-declared constructor would otherwise suppress it) and a
    guarded **copy-from-base** constructor. The latter is what keeps the copy
    protocol faithful for Python subclasses: `__copy__`/`__deepcopy__` re-run the
    registered copy `__init__` on a `type(self).__new__` shell, and pybind11 then
    needs the *alias* type constructible from `const BASE&` — or the copied
    subclass instance would hold a plain @a BASE payload and stop dispatching
    virtuals into Python. It is a constrained template, so a noncopyable @a BASE
    simply leaves the trampoline non-copy-constructible (no error). pybind11 needs
    no per-instance storage (the override is found from `this`), so — unlike the
    nanobind spelling — this adds no data member. Neutral name across the Python
    rods. */
#define WELDER_PY_TRAMPOLINE(TRAMP, BASE)                                      \
    using welder_py_base = BASE;                                              \
    TRAMP() = default;                                                        \
    template <class WelderSrc = BASE>                                         \
        requires ::std::is_copy_constructible_v<WelderSrc>                    \
    explicit TRAMP(const WelderSrc& welder_src)                               \
        : welder_py_base(welder_src) {}                                       \
    using welder_py_base::welder_py_base

/** Body of a single virtual override in a `WELDER_PY_TRAMPOLINE` class, keyed on an
    explicit slot reflection.

    The general form behind @ref WELDER_PY_OVERRIDE, for the case the plain macro
    cannot spell: an **overloaded** virtual, where `^^welder_py_base::FUNC` would name
    an overload set (ill-formed — P2996 has no overload-set reflection). @a SLOT is a
    reflection of the *one* base virtual this override implements — select it with
    @ref welder::rods::python::virtual_slot — and drives the dispatch (name, return
    type, pureness); the textual @a FUNC only spells the qualified base-class
    fallback call, where ordinary overload resolution picks the right overload from
    the forwarded parameters. Parenthesize @a SLOT if the expression contains commas.
    welder's *generated* trampolines use this form for every override. */
#define WELDER_PY_OVERRIDE_AS(SLOT, FUNC, ...)                                 \
    return ::welder::rods::pybind11::override_dispatch<(SLOT)>(                \
        static_cast<const welder_py_base&>(*this),                           \
        [&](auto&&... _welder_a) -> decltype(auto) {                         \
            return welder_py_base::FUNC(                                      \
                static_cast<decltype(_welder_a)&&>(_welder_a)...);           \
        } __VA_OPT__(, ) __VA_ARGS__)

/** Body of a single virtual override in a `WELDER_PY_TRAMPOLINE` class.

    Use as the whole override body: `int legs() const override { WELDER_PY_OVERRIDE(legs); }`.
    Forwards to the Python override of @a FUNC if present, else to `BASE::FUNC`. Extra
    arguments are the method's parameters, forwarded to both paths. @a FUNC must name a
    *single* virtual — for an overloaded one use @ref WELDER_PY_OVERRIDE_AS with a
    @ref welder::rods::python::virtual_slot selection. Neutral name: each Python rod
    defines it against its own dispatch. */
#define WELDER_PY_OVERRIDE(FUNC, ...)                                          \
    WELDER_PY_OVERRIDE_AS(^^welder_py_base::FUNC, FUNC __VA_OPT__(, ) __VA_ARGS__)
