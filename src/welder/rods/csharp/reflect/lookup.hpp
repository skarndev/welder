#pragma once
#include <cstddef>
#include <meta>
#include <optional>
#include <string_view>

#include <welder/bindable.hpp> // public_bases (the flattened-member walk)
#include <welder/diag.hpp>     // csharp_member_lookup_mismatch

/** @file
    The **member-lookup layer**: the one place the generator and the generated
    shim agree on *which* reflected entity a thunk is about.

    The C# backend emits two translation units. The generator computes, for each
    emitted thunk, a small positional key — "the 2nd function member named
    `area`", "constructor #1", "the field named `x`" — and writes that key into
    the shim as a call to one of these functions. The shim, compiled with
    reflection against the same welded header, evaluates it and splices the
    resulting reflection. Neither side ever respells a C++ type or relies on
    overload resolution over wire-typed arguments.

    The keys are deliberately **participation-independent** (they count *all*
    same-named declarations, however the resolution prunes them) so that changing
    a mark cannot silently renumber a thunk. If the header drifts between
    generation and shim compilation the lookup finds nothing and throws
    @ref welder::diag::csharp_member_lookup_mismatch — a constexpr exception,
    which fails the shim build loudly instead of binding the wrong overload.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

/** The member TYPE of @a owner named @a name — the anchor lookup for a nested
    class the generated shim cannot SPELL (a protected nested type's qualified
    name is inaccessible at namespace scope; reflection enumeration is not).
    Aliases count (a class-scope alias anchor), the aliased type is returned
    as-is (the caller dealiases when it must).
    @param owner a reflection of the enclosing class.
    @param name  the nested type's identifier.
    @return the member type's (possibly alias) reflection.
    @throws diag::csharp_member_lookup_mismatch when no such member type
    exists — the generator/shim drift guard, like @ref named_member. */
consteval std::meta::info nested_type(std::meta::info owner,
                                      std::string_view name) {
    for (std::meta::info m : std::meta::members_of(
             owner, std::meta::access_context::unchecked())) {
        if ((std::meta::is_type(m) || std::meta::is_type_alias(m)) &&
            std::meta::has_identifier(m) &&
            std::meta::identifier_of(m) == name)
            return m;
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** The @a k-th function member of @a owner (class or namespace) named @a name,
    counting ALL same-named function declarations in declaration order —
    participation-independent, so the index is stable however the resolution
    prunes. The generator computes an emitted overload's index with @ref
    index_of_named_member; the generated shim calls this to re-derive the same
    reflection and splice it — the exact overload, never overload resolution
    over wire-typed arguments.
    @param owner a reflection of the declaring class or namespace.
    @param name  the member's identifier.
    @param k     the index among same-named function members.
    @return the member's reflection, or `nullopt` when there is none. */
consteval std::optional<std::meta::info> find_named_member(std::meta::info owner,
                                                           std::string_view name,
                                                           std::size_t k) {
    std::size_t seen{0};
    bool declared_here{false};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_function(m) && std::meta::has_identifier(m) &&
            std::meta::identifier_of(m) == name) {
            declared_here = true;
            if (seen == k)
                return m;
            ++seen;
        }
    }
    // The name IS declared in this scope: @a k indexes it, and running out is
    // real drift — do not go looking for a same-named base member, which would
    // silently bind the wrong overload.
    if (declared_here || !std::meta::is_class_type(owner))
        return std::nullopt;
    // Not declared here, so it is a FLATTENED base member (welder binds a
    // non-welded base's members onto the derived type). Recurse per scope, not
    // over a merged sequence: @ref index_of_named_member counts within the
    // member's own declaring class, so the base must be indexed the same way.
    for (std::meta::info b : ::welder::public_bases(owner))
        if (auto found{find_named_member(b, name, k)})
            return found;
    return std::nullopt;
}

/** @ref find_named_member, as the throwing form the generated shim splices.
    @param owner a reflection of the declaring class or namespace.
    @param name  the member's identifier.
    @param k     the index among same-named function members.
    @return the member's reflection.
    @throws diag::csharp_member_lookup_mismatch when no such member exists —
    the header drifted since generation, so the shim build fails loudly. */
consteval std::meta::info named_member(std::meta::info owner,
                                       std::string_view name, std::size_t k) {
    if (auto found{find_named_member(owner, name, k)})
        return *found;
    throw diag::csharp_member_lookup_mismatch{};
}

/** @ref named_member's inverse: the declaration-order index of function @a fn
    among the same-named function members of its declaring scope. The generator
    emits this index; the shim's @ref named_member re-derives the reflection.
    @param fn a reflection of the function member.
    @return its index among the same-named members of `parent_of(fn)`.
    @throws diag::csharp_member_lookup_mismatch when @a fn is not among them. */
consteval std::size_t index_of_named_member(std::meta::info fn) {
    const std::meta::info owner{std::meta::parent_of(fn)};
    const std::string_view name{std::meta::identifier_of(fn)};
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_function(m) && std::meta::has_identifier(m) &&
            std::meta::identifier_of(m) == name) {
            if (m == fn)
                return seen;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** The @a k-th constructor of class @a owner in declaration order (copy/move
    included in the count — raw positions, stable on both sides).
    @param owner a reflection of the class.
    @param k     the declaration-order constructor index.
    @return the constructor's reflection.
    @throws diag::csharp_member_lookup_mismatch when there is no such constructor. */
consteval std::meta::info ctor_at(std::meta::info owner, std::size_t k) {
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_constructor(m)) {
            if (seen == k)
                return m;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** @ref ctor_at's inverse: the declaration-order constructor index of @a ctor.
    @param ctor a reflection of the constructor.
    @return its index among its class's constructors.
    @throws diag::csharp_member_lookup_mismatch when @a ctor is not among them. */
consteval std::size_t index_of_ctor(std::meta::info ctor) {
    const std::meta::info owner{std::meta::parent_of(ctor)};
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_constructor(m)) {
            if (m == ctor)
                return seen;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** The nonstatic data member of @a owner named @a name (unique — fields cannot
    overload), or the namespace-scope variable for a namespace @a owner.
    @param owner a reflection of the class or namespace.
    @param name  the member's identifier.
    @return the member's reflection, or `nullopt` when there is none. */
consteval std::optional<std::meta::info> find_named_field(std::meta::info owner,
                                                          std::string_view name) {
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if ((std::meta::is_nonstatic_data_member(m) ||
             std::meta::is_variable(m)) &&
            std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
            return m;
    }
    // A FLATTENED base's field: welder binds a non-welded base's members onto
    // the derived type, and the generator anchors the lookup on the derived
    // type whenever the declaring scope has no spellable name (a class-template
    // specialization base — `M2Root<V> : DataPreWotlk<V>`). Derived-first, so a
    // shadowing field still wins.
    if (std::meta::is_class_type(owner))
        for (std::meta::info b : ::welder::public_bases(owner))
            if (auto found{find_named_field(b, name)})
                return found;
    return std::nullopt;
}

/** @ref find_named_field, as the throwing form the generated shim splices.
    @param owner a reflection of the class or namespace.
    @param name  the member's identifier.
    @return the member's reflection.
    @throws diag::csharp_member_lookup_mismatch when there is no such member. */
consteval std::meta::info named_field(std::meta::info owner,
                                      std::string_view name) {
    if (auto found{find_named_field(owner, name)})
        return *found;
    throw diag::csharp_member_lookup_mismatch{};
}

/** The declared type of callable @a fn's first parameter — an info-returning
    helper (not a subscript at the use site) because gcc-16 rejects a
    subscripted consteval temporary inside a template argument of a runtime
    expression (the same family of workaround as the variable templates).
    @param fn a reflection of the callable (must have ≥ 1 parameter).
    @return the first parameter's declared type. */
consteval std::meta::info first_param_type(std::meta::info fn) {
    return std::meta::type_of(std::meta::parameters_of(fn)[0]);
}

} // namespace welder::inline v0::rods::csharp
