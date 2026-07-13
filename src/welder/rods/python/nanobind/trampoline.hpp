#pragma once
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

#include <welder/rods/python/trampoline.hpp> // virtual_slot_count, trampoline_for, …

#include <nanobind/nanobind.h>
#include <nanobind/trampoline.h>

/** @file
    nanobind's half of welder's virtual-override support: the reflection-driven
    dispatch helper that replaces `NB_OVERRIDE` / `NB_OVERRIDE_PURE`, and the
    `WELDER_PY_TRAMPOLINE` / `WELDER_PY_OVERRIDE` authoring macros.

    The backend-neutral machinery (slot count, the `trampoline_for` registration
    hook, the coverage check) lives in `<welder/rods/python/trampoline.hpp>`. This
    header only adds what is specific to nanobind's runtime: its `detail::trampoline`
    storage and `detail::ticket` lookup. The macros are spelled the same as
    pybind11's (a translation unit includes exactly one Python rod), so a single
    trampoline definition compiles under either backend.

    Include this from a nanobind binding translation unit *before* the header that
    defines your trampoline subclass.
*/

namespace welder::inline v0::rods::nanobind {

// The unqualified name `nanobind` resolves to *this* namespace inside it; alias the
// real library the way nanobind's own docs do (see rod.hpp).
namespace nb = ::nanobind;

/** Dispatch a captured virtual call to its Python override, else to the C++ base.

    The reflection-driven replacement for `NB_OVERRIDE`/`NB_OVERRIDE_PURE`: the
    method name (for the Python attribute lookup), the return type, and whether the
    method is pure are all read from @a Fn — a reflection of the *base* virtual —
    rather than spelled by hand.

    The base-class fallback is passed in as @a base_call rather than derived from
    @a Fn: splicing a member function into a member access (`self.[:Fn:]()`) performs
    a *virtual* call, which would re-enter the override and recurse. The caller
    (`WELDER_PY_OVERRIDE`) supplies a lambda doing a textually *qualified*
    `Base::method(...)` call, which is non-virtual — matching what `NB_OVERRIDE`'s
    `NBBase::func(...)` does.

    @tparam Fn a reflection of the base virtual member function.
    @tparam N  the trampoline's slot count.
    @param tr        the nanobind trampoline storage (a `const` ref: it is accessed
                     from `const` overrides and its cache is `mutable`).
    @param base_call an invocable performing the qualified base-class call.
    @param args      the forwarded call arguments. */
template <auto Fn, std::size_t N, typename BaseCall, typename... Args>
decltype(auto) override_dispatch(const nb::detail::trampoline<N>& tr,
                                 BaseCall&& base_call, Args&&... args) {
    using ret_type = [:std::meta::return_type_of(Fn):];
    static_assert(
        !std::is_reference_v<ret_type>,
        "welder: cannot trampoline a virtual method that returns a reference "
        "(nanobind cannot keep the referent alive across the C++/Python boundary); "
        "return by value, or mark the type [[=welder::rods::python::bind_flat]].");

    constexpr const char* name{
        std::define_static_string(std::meta::identifier_of(Fn))};

    if constexpr (std::meta::is_pure_virtual(Fn)) {
        // Pure: there is no base to fall back to. nanobind's trampoline machinery
        // raises a descriptive error if Python supplied no override.
        nb::detail::ticket t(tr, name, /*pure=*/true);
        return nb::cast<ret_type>(tr.base().attr(t.key)(std::forward<Args>(args)...));
    } else {
        nb::detail::ticket t(tr, name, /*pure=*/false);
        if (t.key.is_valid())
            return nb::cast<ret_type>(
                tr.base().attr(t.key)(std::forward<Args>(args)...));
        return std::forward<BaseCall>(base_call)(std::forward<Args>(args)...);
    }
}

} // namespace welder::rods::nanobind

/** Declare a class a nanobind trampoline for @a BASE.

    Place at the top of a trampoline subclass body. Introduces the `welder_py_base`
    alias the override macro keys off, inherits @a BASE's constructors, and adds the
    nanobind trampoline storage sized by reflection — the `N` in the slot count never
    drifts from @a BASE's virtuals. Neutral name: pybind11's rod defines the same
    macro without a storage member. */
#define WELDER_PY_TRAMPOLINE(BASE)                                             \
    using welder_py_base = BASE;                                              \
    using welder_py_base::welder_py_base;                                     \
    ::nanobind::detail::trampoline<                                           \
        ::welder::rods::python::virtual_slot_count(^^BASE)>                   \
        welder_nb_trampoline{this}

/** Body of a single virtual override in a `WELDER_PY_TRAMPOLINE` class.

    Use as the whole override body: `int legs() const override { WELDER_PY_OVERRIDE(legs); }`.
    Forwards to the Python override of @a FUNC if present, else to `BASE::FUNC`. Extra
    arguments are the method's parameters, forwarded to both paths. Neutral name:
    each Python rod defines it against its own dispatch. */
#define WELDER_PY_OVERRIDE(FUNC, ...)                                          \
    return ::welder::rods::nanobind::override_dispatch<^^welder_py_base::FUNC>( \
        this->welder_nb_trampoline,                                          \
        [&](auto&&... _welder_a) -> decltype(auto) {                         \
            return welder_py_base::FUNC(                                      \
                static_cast<decltype(_welder_a)&&>(_welder_a)...);           \
        } __VA_OPT__(, ) __VA_ARGS__)
