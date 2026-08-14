#pragma once
#include <cstddef>
#include <meta>
#include <string_view>
#include <vector>

#include <welder/diag.hpp> // no_matching_virtual_slot

/** @file
    welder's backend-neutral **virtual-slot machinery**: which virtual member
    functions of a type are *overridable from the target language*, and the
    `bind_flat` opt-out marker.

    Very different backends consume the same answers: the **Python** rods
    route these slots through hand-authored trampolines (the authoring macros
    and the trampoline registration live in
    `<welder/rods/python/trampoline.hpp>`, which re-exports everything here
    under its historical `welder::rods::python` spellings), while out-of-tree
    rods can generate e.g. *director* subclasses over the identical slot set.
    Keeping the slot semantics in one
    place is what makes "overridable" mean the same thing in every language:
    vtable-slot identity (covariant overrides fold to one slot), inherited
    virtuals folded in, protected NVI hooks included, private declarations
    withdrawing their slot, and `bind_flat` opting a type or method out.

    Like the rest of the reflection layer this is header-only, std-`<meta>`
    based, and free of any framework dependency.
*/

namespace welder::inline v0 {

// --- bind_flat: opt out of trampoline support for a virtual type -------------

/** The stored form of a `bind_flat` mark (a plain tag — it carries no state).

    welder binds a welded type that has virtual methods as overridable by default and
    *requires* an override vehicle — a Python trampoline (`welder::rods::python::trampoline_for`) or a generated C# director
    — and this mark vouches that the
    entity is intentionally bound non-overridably and suppresses that requirement. It
    applies at two granularities — see #bind_flat. */
struct bind_flat_spec {};

/** Mark a virtual entity as deliberately bound non-overridably.

    Usage: `[[=welder::bind_flat]]`. Two granularities:

    - On a **type**: the whole type is bound non-overridably (produced by C++, never
      subclassed in Python) — no trampoline is required, and none of its virtuals are
      exposed for Python override.
    - On a **member function**: only that virtual is bound flat — it stays a plain
      bound method, drops out of the trampoline's slot count and coverage
      requirement, while the type's *other* virtuals remain overridable. Useful to
      exclude a single virtual (e.g. one returning a reference) from override routing.

    Without it, every welded type carrying an overridable virtual must register a
    trampoline via `welder::rods::python::trampoline_for`, or welder's Python rods reject it at compile
    time. @see bind_flat_spec */
inline constexpr bind_flat_spec bind_flat{};

/** Does @a entity (a type or a member function) carry a `bind_flat` mark?
    @param entity a reflection of the type or member to test.
    @return `true` iff @a entity opts out of trampoline / override routing. */
consteval bool bound_flat(std::meta::info entity) {
    return !std::meta::annotations_of_with_type(entity, ^^bind_flat_spec).empty();
}

// --- virtual-method reflection ----------------------------------------------

/** Is @a member an overridable virtual — a virtual member function that welder
    routes through the trampoline?

    Excludes the (virtual) destructor, which is not an overridable slot, and any
    method marked @ref bind_flat, which is bound as a plain non-overridable method.
    A *per-declaration* predicate; the whole-type slot set is @ref
    overridable_virtuals, which additionally folds inherited virtuals in.
    @param member a reflection of a class member.
    @return `true` iff @a member is a virtual method exposed for Python override. */
consteval bool is_overridable_virtual(std::meta::info member) {
    return std::meta::is_function(member) && std::meta::is_virtual(member) &&
           !std::meta::is_destructor(member) && !bound_flat(member);
}

namespace detail {

/** Do @a a and @a b declare the *same vtable slot*?

    Slot identity is the name plus everything overriding keys off: the parameter
    types and the cv / ref qualifiers. The **return type** is deliberately excluded —
    a covariant override (`Derived* clone()` over `Base* clone()`) redeclares the
    same slot with a narrower return, and comparing full `type_of` would count it as
    a second slot. `noexcept` is excluded for the same reason: an override may
    *strengthen* the exception specification, and two overloads cannot differ by
    `noexcept` alone, so it can never distinguish genuine slots. */
consteval bool same_slot(std::meta::info a, std::meta::info b) {
    if (std::meta::identifier_of(a) != std::meta::identifier_of(b))
        return false;
    if (std::meta::is_const(a) != std::meta::is_const(b) ||
        std::meta::is_volatile(a) != std::meta::is_volatile(b) ||
        std::meta::is_lvalue_reference_qualified(a) !=
            std::meta::is_lvalue_reference_qualified(b) ||
        std::meta::is_rvalue_reference_qualified(a) !=
            std::meta::is_rvalue_reference_qualified(b))
        return false;
    auto pa{std::meta::parameters_of(a)};
    auto pb{std::meta::parameters_of(b)};
    if (pa.size() != pb.size())
        return false;
    for (std::size_t i{0}; i < pa.size(); ++i)
        if (std::meta::type_of(pa[i]) != std::meta::type_of(pb[i]))
            return false;
    return true;
}

/** Accumulate the *most-derived* declaration of every virtual member function
    reachable in @a type's complete object into @a slots, deduplicating by vtable
    slot (@ref same_slot — name + parameters + cv/ref, so a covariant override is
    one slot, not two).

    `members_of` lists a class's *own* members only, so a virtual a subclass merely
    inherits is invisible there — the reason a whole-hierarchy walk is needed. @a
    type's own members are visited before its bases, so the first declaration recorded
    for a given slot is the most-derived one; a base's re-declaration of an
    already-seen slot is skipped. Members are enumerated with
    `access_context::unchecked()` (like the rest of welder's core): a **protected**
    virtual — the NVI/template-method hook — is a real overridable slot even though
    it is never *bound* (Python overrides it via attribute lookup, which needs no
    binding). Neither @ref bind_flat nor access is *filtered* here — a **private**
    declaration still claims its slot in the dedup, and the filters are applied by
    @ref overridable_virtuals on the kept most-derived declaration. So a subclass
    can un-flatten a virtual its base marked `bind_flat` (and vice versa), and
    privatizing an inherited virtual withdraws the slot rather than resurrecting
    the base's public declaration.
    @param type  a reflection of the class type.
    @param slots the accumulator of most-derived virtual declarations. */
consteval void collect_virtuals(std::meta::info type,
                                std::vector<std::meta::info>& slots) {
    for (auto m :
         std::meta::members_of(type, std::meta::access_context::unchecked())) {
        if (!std::meta::is_function(m) || !std::meta::is_virtual(m) ||
            std::meta::is_destructor(m) || !std::meta::has_identifier(m))
            continue;
        bool seen{false};
        for (auto s : slots)
            if (same_slot(s, m)) {
                seen = true;
                break;
            }
        if (!seen)
            slots.push_back(m);
    }
    for (auto b :
         std::meta::bases_of(type, std::meta::access_context::unchecked()))
        collect_virtuals(std::meta::dealias(std::meta::type_of(b)), slots);
}

} // namespace detail

/** Every overridable virtual slot of @a type — the ones welder routes through a
    trampoline — folding in virtuals inherited from any base.

    Unlike a bare `members_of` scan this sees a virtual @a type only *inherits* (never
    re-declares), which a trampoline must still cover: a Python subclass of @a type can
    override it, and dispatch runs through @a type's own trampoline, not the base's. A
    slot is its vtable identity — name + parameter types + cv/ref qualifiers, so a
    **covariant** override is the same slot, kept with its narrowed return type; when a
    class overrides an inherited virtual, only the most-derived declaration is kept,
    and its @ref bind_flat mark (not the base's) decides whether the slot is exposed.
    **Protected** virtuals (the NVI hook pattern) are slots like any other — a Python
    subclass overrides them by plain attribute lookup, no binding involved. **Private**
    declarations are excluded: the trampoline's base-class fallback could not name
    them; a subclass privatizing an inherited virtual thereby withdraws the slot.
    @param type a reflection of the class type.
    @return the most-derived declaration of each exposed overridable virtual. */
consteval std::vector<std::meta::info> overridable_virtuals(std::meta::info type) {
    // Accept an alias reflection too (the spelling a generated trampoline uses for
    // a class-template specialization): the walk needs the underlying class.
    type = std::meta::dealias(type);
    std::vector<std::meta::info> all{};
    detail::collect_virtuals(type, all);
    std::vector<std::meta::info> out{};
    for (auto s : all)
        if (!bound_flat(s) && !std::meta::is_private(s))
            out.push_back(s);
    return out;
}

/** The overridable virtual slot of @a type named @a name whose function type is
    @a fn_type — the hand-written disambiguator for an **overloaded** virtual.

    `^^Base::fn` is ill-formed when `fn` names an overload set (P2996 has no
    overload-set reflection), so `WELDER_PY_OVERRIDE(fn)` cannot be used inside the
    override of an overloaded virtual. This finder selects one overload by its exact
    function type, for the slot-taking macro form:

    @code
    std::string send(int code) const override {
        WELDER_PY_OVERRIDE_AS(
            (welder::virtual_slot(^^Robot, "send", ^^std::string(int) const)),
            send, code);
    }
    @endcode

    (The extra parentheses keep any commas inside the expression out of the
    preprocessor's argument splitting.) Searches @ref overridable_virtuals, so
    inherited slots are found too.
    @param type    a reflection of the welded type.
    @param name    the virtual's identifier.
    @param fn_type a reflection of the overload's full function type, trailing
                   qualifiers included (e.g. `^^int(int) const`).
    @return the matching slot's reflection.
    @throws diag::no_matching_virtual_slot (a constant-evaluation error) when
            no slot matches the name/type pair. */
consteval std::meta::info virtual_slot(std::meta::info type, std::string_view name,
                                       std::meta::info fn_type) {
    for (auto s : overridable_virtuals(type))
        if (std::meta::identifier_of(s) == name &&
            std::meta::type_of(s) == std::meta::dealias(fn_type))
            return s;
    throw diag::no_matching_virtual_slot{};
}

/** The number of overridable virtual member functions of @a type, inherited ones
    included.

    This is the `N` in `NB_TRAMPOLINE(Base, N)` / a trampoline's slot count — the
    virtuals actually routed through the trampoline, so per-method @ref bind_flat
    marks (and the destructor) are excluded. Counts inherited virtuals too (see
    @ref overridable_virtuals), so a subclass whose virtuals all come from a base still
    reports a non-zero count and gets a correctly sized trampoline.
    @param type a reflection of the class type.
    @return the count of @a type's overridable virtual member functions. */
consteval std::size_t virtual_slot_count(std::meta::info type) {
    return overridable_virtuals(type).size();
}

/** Does @a type declare or inherit any overridable virtual method?
    @param type a reflection of the class type.
    @return `true` iff @a type has at least one overridable virtual slot. */
consteval bool has_virtual_methods(std::meta::info type) {
    return !overridable_virtuals(type).empty();
}

} // namespace welder::inline v0
