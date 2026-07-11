#pragma once
#include <cstddef>
#include <meta>

#include <welder/vocabulary.hpp> // the annotation vocabulary (for structural specs)

/** @file
    Virtual-function overriding support shared by welder's Python backends.

    pybind11 and nanobind both let a Python subclass override a C++ `virtual`
    method, but only if the class is bound with a *trampoline* — a C++ subclass that
    captures each virtual call and forwards it to Python (nanobind's `NB_TRAMPOLINE`
    / `PYBIND11_OVERRIDE`). welder cannot *synthesize* that subclass: generating the
    override declarations needs member-declaration injection, which C++26 reflection
    (P2996) does not provide (its only class-synthesis facility,
    `std::meta::define_aggregate`, adds data members only). The vtable also forces
    each override to be a real member function sharing the base method's exact name.

    So the trampoline is still hand-authored — but reflection automates everything
    *around* it, and this header holds the backend-neutral half:

    - @ref virtual_slot_count / @ref has_virtual_methods — how many overridable slots
      a type has (the `NB_TRAMPOLINE(Base, N)` count, never hand-maintained);
    - @ref trampoline_for — the user's `T → trampoline` registration hook (a
      specializable variable template, the type-level analogue of
      `welder::trust_bindable`), read by each Python rod's class-creation primitive
      to bind `class_<T, Trampoline>` instead of `class_<T>`;
    - @ref bind_flat — the opt-out marker for a virtual type that is deliberately
      bound non-overridably (C++-produced, never subclassed in Python);
    - @ref trampoline_covers — the compile-time coverage check (every virtual of `T`
      is overridden in the trampoline), so a forgotten override is a build error, not
      a method that silently never reaches Python.

    The per-override dispatch body and the authoring macros are backend-specific and
    live in each rod's own `trampoline.hpp` (`welder/rods/python/nanobind/…`,
    `…/pybind11/…`); those spell the neutral `WELDER_PY_TRAMPOLINE` /
    `WELDER_PY_OVERRIDE` differently so one trampoline source compiles under either
    Python rod.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`), like
    the rest of the reflection layer.
*/

namespace welder::rods::python {

// --- bind_flat: opt out of trampoline support for a virtual type -------------

/** The stored form of a `bind_flat` mark (a plain tag — it carries no state).

    welder binds a welded type that has virtual methods as overridable by default and
    *requires* a trampoline (see @ref trampoline_for); this mark vouches that the
    entity is intentionally bound non-overridably and suppresses that requirement. It
    applies at two granularities — see #bind_flat. */
struct bind_flat_spec {};

/** Mark a virtual entity as deliberately bound non-overridably.

    Usage: `[[=welder::rods::python::bind_flat]]`. Two granularities:

    - On a **type**: the whole type is bound non-overridably (produced by C++, never
      subclassed in Python) — no trampoline is required, and none of its virtuals are
      exposed for Python override.
    - On a **member function**: only that virtual is bound flat — it stays a plain
      bound method, drops out of the trampoline's slot count and coverage
      requirement, while the type's *other* virtuals remain overridable. Useful to
      exclude a single virtual (e.g. one returning a reference) from override routing.

    Without it, every welded type carrying an overridable virtual must register a
    trampoline via @ref trampoline_for, or welder's Python rods reject it at compile
    time. @see bind_flat_spec */
inline constexpr bind_flat_spec bind_flat{};

/** Does @a entity (a type or a member function) carry a `bind_flat` mark?
    @param entity a reflection of the type or member to test.
    @return `true` iff @a entity opts out of trampoline / override routing. */
consteval bool bound_flat(std::meta::info entity) {
    return !std::meta::annotations_of_with_type(entity, ^^bind_flat_spec).empty();
}

// --- trampoline registration -------------------------------------------------

/** The trampoline subclass registered for @a T, or a null reflection if none.

    Specialize this in the binding translation unit — where the concrete,
    backend-specific trampoline is defined — to tell welder's Python rods to bind
    `class_<T, Trampoline>` so Python subclasses can override @a T's virtual methods:

    @code
    template <> constexpr std::meta::info
        welder::rods::python::trampoline_for<Animal> = ^^PyAnimal;
    @endcode

    It is the type-level counterpart of @ref welder::trust_bindable — a hook usable
    even for third-party types you cannot annotate. @tparam T the welded type. */
template <class T>
constexpr std::meta::info trampoline_for = std::meta::info{};

/** The type welder constructs when binding @a T: its registered trampoline if one
    exists, else @a T itself.

    A rod exposes this (as `construction_type<T>`) so the driver decides
    constructibility against the concrete trampoline rather than @a T. That matters
    for an **abstract** base: `std::is_default_constructible_v<T>` is `false`, so the
    driver would register no constructor and a Python subclass could not be
    instantiated — but the trampoline *is* constructible, so binding it keeps the
    subclass usable. For a concrete @a T the trampoline is constructible exactly when
    @a T is, so the substitution changes nothing.
    @tparam T the welded type.
    @return a reflection of the trampoline type, or of @a T if none is registered. */
template <class T>
consteval std::meta::info construction_type_of() {
    return trampoline_for<T> != std::meta::info{} ? trampoline_for<T> : ^^T;
}

// --- virtual-method reflection ----------------------------------------------

/** Is @a member an overridable virtual — a virtual member function that welder
    routes through the trampoline?

    Excludes the (virtual) destructor, which is not an overridable slot, and any
    method marked @ref bind_flat, which is bound as a plain non-overridable method.
    @param member a reflection of a class member.
    @return `true` iff @a member is a virtual method exposed for Python override. */
consteval bool is_overridable_virtual(std::meta::info member) {
    return std::meta::is_function(member) && std::meta::is_virtual(member) &&
           !std::meta::is_destructor(member) && !bound_flat(member);
}

/** The number of overridable virtual member functions of @a type.

    This is the `N` in `NB_TRAMPOLINE(Base, N)` / a trampoline's slot count — the
    virtuals actually routed through the trampoline, so per-method @ref bind_flat
    marks (and the destructor) are excluded.
    @param type a reflection of the class type.
    @return the count of @a type's overridable virtual member functions. */
consteval std::size_t virtual_slot_count(std::meta::info type) {
    std::size_t n{0};
    for (auto m :
         std::meta::members_of(type, std::meta::access_context::current())) {
        if (is_overridable_virtual(m)) {
            ++n;
        }
    }
    return n;
}

/** Does @a type declare or inherit any overridable virtual method?
    @param type a reflection of the class type.
    @return `true` iff @a type has at least one overridable virtual slot. */
consteval bool has_virtual_methods(std::meta::info type) {
    return virtual_slot_count(type) > 0;
}

/** Does @a trampoline declare an override for the virtual method @a vfn?

    A trampoline overrides `vfn` by redeclaring a member function with the same name
    and the *same signature*; `members_of` lists a class's own members (not inherited
    ones), so an un-overridden virtual is simply absent. Matching is by name plus
    `type_of` equality — the reflected function type, which bundles the parameter
    types, cv-qualification, and ref-qualifier (independent of the declaring class).
    That distinguishes a real override from an unrelated same-named overload or a
    const/ref-qualifier mismatch, which name-plus-arity alone would not.

    @note A *covariant* override (narrower return type) has a different function type
    and so is not recognized here; such a type must opt out with @ref bind_flat.
    @param trampoline a reflection of the trampoline subclass.
    @param vfn        a reflection of a base virtual member function.
    @return `true` iff @a trampoline redeclares @a vfn with a matching signature. */
consteval bool declares_override(std::meta::info trampoline, std::meta::info vfn) {
    auto name{std::meta::identifier_of(vfn)};
    auto sig{std::meta::type_of(vfn)};
    for (auto m :
         std::meta::members_of(trampoline, std::meta::access_context::current())) {
        if (std::meta::is_function(m) && !std::meta::is_special_member_function(m) &&
            std::meta::identifier_of(m) == name && std::meta::type_of(m) == sig) {
            return true;
        }
    }
    return false;
}

/** Does @a trampoline override every virtual method of @a type?

    The coverage guard behind welder's compile-time check: a virtual left
    un-overridden would bind, but calls to it from C++ would never dispatch into a
    Python override. @param type a reflection of the welded base type. @param
    trampoline a reflection of its registered trampoline. @return `true` iff every
    overridable virtual of @a type is redeclared in @a trampoline. */
consteval bool trampoline_covers(std::meta::info type, std::meta::info trampoline) {
    for (auto m :
         std::meta::members_of(type, std::meta::access_context::current())) {
        if (is_overridable_virtual(m) && !declares_override(trampoline, m)) {
            return false;
        }
    }
    return true;
}

} // namespace welder::rods::python