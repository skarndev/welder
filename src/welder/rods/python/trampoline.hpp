#pragma once
#include <cstddef>
#include <meta>
#include <string_view>
#include <vector>

#include <welder/diag.hpp>       // the consteval diagnostics (no_matching_virtual_slot)
#include <welder/virtuals.hpp>   // the neutral slot machinery (welder::virtual_slot, ...)
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
    *around* it. The slot machinery itself (@ref welder::virtual_slot_count /
    @ref welder::has_virtual_methods — the `NB_TRAMPOLINE(Base, N)` count, never
    hand-maintained — @ref welder::overridable_virtuals, the
    @ref welder::bind_flat opt-out marker) is language-neutral and lives in
    `<welder/virtuals.hpp>`; this header holds the PYTHON-side half:

    - @ref welder::rods::python::trampoline_for "trampoline_for" — the user's
      `T → trampoline` registration hook (a specializable variable template, the
      type-level analogue of `welder::trust_bindable`), read by each Python rod's
      class-creation primitive to bind `class_<T, Trampoline>` instead of `class_<T>`;
    - @ref welder::rods::python::trampoline_covers "trampoline_covers" — the
      compile-time coverage check (every virtual of `T` is overridden in the
      trampoline), so a forgotten override is a build error, not a method that
      silently never reaches Python.

    The per-override dispatch body and the authoring macros are backend-specific and
    live in each rod's own `trampoline.hpp` (`welder/rods/python/nanobind/…`,
    `…/pybind11/…`); those spell the neutral `WELDER_PY_TRAMPOLINE` /
    `WELDER_PY_OVERRIDE` differently so one trampoline source compiles under either
    Python rod.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`), like
    the rest of the reflection layer.
*/

namespace welder::inline v0::rods::python {

// The backend-neutral slot machinery (bind_flat, overridable_virtuals,
// virtual_slot, ...) is language-neutral — the C# rod's directors consume the
// same answers — and lives at welder:: scope in <welder/virtuals.hpp> (spell
// it [[=welder::bind_flat]], welder::virtual_slot(...), ...).

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
    even for third-party types you cannot annotate, and it takes precedence over the
    annotation form (@ref trampoline). @tparam T the welded type. */
template <class T>
constexpr std::meta::info trampoline_for = std::meta::info{};

// --- trampoline discovery by annotation -------------------------------------

/** The stored form of a `trampoline` mark (a plain tag — it carries no state).
    @see #trampoline */
struct trampoline_spec {};

/** Mark a class as the trampoline for the base it derives from — the annotation form
    of @ref trampoline_for.

    Usage: `struct [[=welder::rods::python::trampoline]] PyAnimal : Animal { … };`.
    welder infers the base from the trampoline's own base list and discovers the
    trampoline by scanning that base's namespace (see @ref scanned_trampoline_of), so
    no explicit `T → trampoline` mapping is written. Requires the trampoline to live in
    the **same namespace** as its welded base (reflection offers no global type
    enumeration, so discovery must scan a known scope). For a third-party base, a
    trampoline kept in a different namespace, or to disambiguate, specialize
    @ref trampoline_for instead — it wins when both are present. @see trampoline_spec */
inline constexpr trampoline_spec trampoline{};

/** Does @a type carry a `trampoline` mark?
    @param type a reflection of the class to test.
    @return `true` iff @a type is annotated as a trampoline. */
consteval bool is_trampoline(std::meta::info type) {
    return !std::meta::annotations_of_with_type(type, ^^trampoline_spec).empty();
}

/** The outcome of scanning a base's namespace for its `trampoline`-annotated
    subclass. */
struct scanned_trampoline {
    std::meta::info type{};  /**< the trampoline, or a null reflection if none found. */
    bool ambiguous{false};   /**< `true` iff more than one candidate was found. */
};

/** Find the `trampoline`-annotated class deriving directly from @a base by scanning
    @a base's enclosing namespace.

    The discovery half of the annotation form (@ref trampoline): with no global type
    enumeration in reflection, a trampoline is reachable from its base only by scanning
    a known scope — here @a base's own namespace. Zero matches → a null reflection; two
    or more → @ref scanned_trampoline::ambiguous (resolve with @ref trampoline_for).
    @param base a reflection of the welded base type.
    @return the scan outcome. */
consteval scanned_trampoline scanned_trampoline_of(std::meta::info base) {
    scanned_trampoline result{};
    for (auto mem : std::meta::members_of(std::meta::parent_of(base),
                                          std::meta::access_context::current())) {
        if (mem == base || !std::meta::is_type(mem) || !is_trampoline(mem))
            continue;
        for (auto b :
             std::meta::bases_of(mem, std::meta::access_context::current())) {
            if (std::meta::dealias(std::meta::type_of(b)) ==
                std::meta::dealias(base)) {
                if (result.type != std::meta::info{})
                    result.ambiguous = true;
                else
                    result.type = mem;
                break;
            }
        }
    }
    return result;
}

/** The type welder constructs when binding @a T: its registered/annotated trampoline
    if one exists, else @a T itself.

    A rod exposes this (as `construction_type<T>`) so the driver decides
    constructibility against the concrete trampoline rather than @a T. That matters
    for an **abstract** base: `std::is_default_constructible_v<T>` is `false`, so the
    driver would register no constructor and a Python subclass could not be
    instantiated — but the trampoline *is* constructible, so binding it keeps the
    subclass usable. For a concrete @a T the trampoline is constructible exactly when
    @a T is, so the substitution changes nothing.
    @tparam T the welded type.
    @return a reflection of the trampoline type (registered via @ref trampoline_for or
            discovered via the @ref trampoline annotation), or of @a T if none. */
template <class T>
consteval std::meta::info construction_type_of() {
    if (trampoline_for<T> != std::meta::info{})
        return trampoline_for<T>;
    // Only a virtual type can carry a trampoline — skip the namespace scan otherwise.
    if (has_virtual_methods(^^T))
        if (auto scanned{scanned_trampoline_of(^^T)};
            scanned.type != std::meta::info{})
            return scanned.type;
    return ^^T;
}

/** Does @a trampoline declare an override for the virtual method @a vfn?

    A trampoline overrides `vfn` by redeclaring a member function with the same name
    and the *same signature*; `members_of` lists a class's own members (not inherited
    ones), so an un-overridden virtual is simply absent. Matching is by name plus
    `type_of` equality — the reflected function type, which bundles the parameter
    types, cv-qualification, and ref-qualifier (independent of the declaring class).
    That distinguishes a real override from an unrelated same-named overload or a
    const/ref-qualifier mismatch, which name-plus-arity alone would not.

    @note The slot @a vfn is the *most-derived* declaration (@ref
    overridable_virtuals), so for a covariant chain the trampoline redeclares the
    narrowed signature — which is what a hand-written override must spell anyway.
    @param tramp a reflection of the trampoline subclass.
    @param vfn   a reflection of a base virtual member function.
    @return `true` iff @a tramp redeclares @a vfn with a matching signature. */
consteval bool declares_override(std::meta::info tramp, std::meta::info vfn) {
    auto name{std::meta::identifier_of(vfn)};
    auto sig{std::meta::type_of(vfn)};
    for (auto m :
         std::meta::members_of(tramp, std::meta::access_context::unchecked())) {
        if (std::meta::is_function(m) && !std::meta::is_special_member_function(m) &&
            std::meta::identifier_of(m) == name && std::meta::type_of(m) == sig) {
            return true;
        }
    }
    return false;
}

/** Does @a tramp override every virtual method of @a type — inherited ones included?

    The coverage guard behind welder's compile-time check: a virtual left
    un-overridden would bind, but calls to it from C++ would never dispatch into a
    Python override. Iterates @ref overridable_virtuals, so a virtual @a type merely
    *inherits* must also be redeclared in @a tramp (its dispatch runs through @a type's
    own trampoline, not the base's). @a tramp is scanned by @ref declares_override,
    which lists @a tramp's *own* members — so the trampoline is expected to redeclare
    every override itself, inherited slots included. @param type a reflection of the
    welded base type. @param tramp a reflection of its registered trampoline. @return
    `true` iff every overridable virtual of @a type is redeclared in @a tramp. */
consteval bool trampoline_covers(std::meta::info type, std::meta::info tramp) {
    for (auto m : overridable_virtuals(type))
        if (!declares_override(tramp, m))
            return false;
    return true;
}

} // namespace welder::rods::python