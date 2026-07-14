#pragma once
#include <meta>

/** @file
    Language-agnostic resolution: given a reflected type/member and a target
    language, decide what participates in binding. Backends consume these
    predicates and never re-implement the annotation semantics.

    @note This header depends on the welder vocabulary (`welder::detail::weld_spec`,
    `lang`, …) but deliberately does NOT include `<welder/annotations.hpp>`.
    Provide the vocabulary first — `#include <welder/vocabulary.hpp>` — then
    include this header.
*/

namespace welder::inline v0 {

/** Is @a type welded for language @a L — i.e. does it carry a matching `weld`
    annotation?

    @param type a reflection of the type to test.
    @param L    the target language.
    @return `true` iff @a type's `weld` annotation lists @a L.
*/
consteval bool welded_for(std::meta::info type, lang L) {
    auto anns{std::meta::annotations_of_with_type(type, ^^detail::weld_spec)};
    return !anns.empty() &&
           (std::meta::extract<detail::weld_spec>(anns[0]).mask & lang_bit(L)) != 0;
}

/** The reflection policy declared on @a type, defaulting to `automatic`.

    @param type a reflection of the type to inspect.
    @return the type's `policy` annotation, or `policy_kind::automatic` if none.
*/
consteval policy_kind policy_of(std::meta::info type) {
    auto anns{std::meta::annotations_of_with_type(type, ^^detail::policy_spec)};
    return anns.empty() ? policy_kind::automatic
                        : std::meta::extract<detail::policy_spec>(anns[0]).kind;
}

/** Does @a member carry an `exclude` mark covering language @a L?

    @param member a reflection of the member to test.
    @param L      the target language.
    @return `true` iff an `exclude` mark applies to @a L (a mark with mask `0`
            covers all languages).
*/
consteval bool excluded_for(std::meta::info member, lang L) {
    for (auto a : std::meta::annotations_of_with_type(member, ^^detail::exclude_spec)) {
        auto s{std::meta::extract<detail::exclude_spec>(a)};
        if (s.mask == 0 || (s.mask & lang_bit(L)) != 0)
            return true;
    }
    return false;
}

/** Does @a member carry an `include` mark covering language @a L?

    @param member a reflection of the member to test.
    @param L      the target language.
    @return `true` iff an `include` mark applies to @a L (a mark with mask `0`
            covers all languages).
*/
consteval bool included_for(std::meta::info member, lang L) {
    for (auto a : std::meta::annotations_of_with_type(member, ^^detail::include_spec)) {
        auto s{std::meta::extract<detail::include_spec>(a)};
        if (s.mask == 0 || (s.mask & lang_bit(L)) != 0)
            return true;
    }
    return false;
}

/** Does @a member carry a `trust_bindable` mark covering language @a L?

    The user's vouch that the member's type is registered/convertible outside
    welder's view, so the bindability gate should trust it. See
    `<welder/annotations.hpp>`.

    @param member a reflection of the member to test.
    @param L      the target language.
    @return `true` iff a `trust_bindable` mark applies to @a L (mask `0` covers
            all languages).
*/
consteval bool trusted_for(std::meta::info member, lang L) {
    for (auto a : std::meta::annotations_of_with_type(member, ^^detail::trust_bindable_spec)) {
        auto s{std::meta::extract<detail::trust_bindable_spec>(a)};
        if (s.mask == 0 || (s.mask & lang_bit(L)) != 0)
            return true;
    }
    return false;
}

/** The return-value policy declared on callable @a fn for language @a L.

    Reads @a fn's `return_policy` annotations, honoring the first one whose mask
    covers @a L (a mask of `0` covers all languages). Absent any, the policy is
    @ref rv_kind::automatic — the rod's default, so an unannotated callable binds
    exactly as before. A rod with an explicit policy knob (the Python backends)
    translates the result; the Lua rods ignore it (ownership is structural).

    @param fn a reflection of the function/method/operator.
    @param L  the target language.
    @return the resolved policy, or @ref rv_kind::automatic if none applies to @a L.
*/
consteval rv_kind return_policy_of(std::meta::info fn, lang L) {
    for (auto a : std::meta::annotations_of_with_type(fn, ^^detail::return_policy_spec)) {
        auto s{std::meta::extract<detail::return_policy_spec>(a)};
        if (s.mask == 0 || (s.mask & lang_bit(L)) != 0)
            return s.kind;
    }
    return rv_kind::automatic;
}

namespace detail {
/** Diagnostic anchor, never defined: a `return_policy` requesting a
    reference-category policy (`reference` / `reference_internal`) on a callable
    that returns *by value* would bind a reference to a temporary. Naming this in
    constant evaluation is the compile error that says so — drop the policy, or
    return a pointer/reference. */
void return_policy_binds_a_reference_to_a_returned_temporary();
} // namespace detail

/** Reject a `return_policy` on @a Fn (for language @a L) that contradicts @a Fn's
    return type.

    A reference-category policy (`reference` / `reference_internal`) promises the
    target a view into a live C++ object; on a by-value (prvalue) return there is
    no such object — only a temporary — so the view would dangle. This is a
    contradiction in *any* language, so every rod (Python and Lua alike) runs the
    check at its per-overload bind site. Anything the framework could plausibly
    honor passes; only the dangling case is diagnosed.

    @tparam Fn a reflection of the callable being bound.
    @tparam L  the target language. */
template <std::meta::info Fn, lang L>
consteval void validate_return_policy() {
    constexpr rv_kind k{return_policy_of(Fn, L)};
    if constexpr (k == rv_kind::reference || k == rv_kind::reference_internal) {
        constexpr std::meta::info rt{std::meta::return_type_of(Fn)};
        if constexpr (!(std::meta::is_pointer_type(rt) ||
                        std::meta::is_lvalue_reference_type(rt) ||
                        std::meta::is_rvalue_reference_type(rt)))
            detail::return_policy_binds_a_reference_to_a_returned_temporary();
    }
}

namespace detail {
/** Diagnostic anchor, never defined: a bare `[[=welder::mark::only]]` is
    meaningless ("only, for every language" restricts nothing) — naming this in
    constant evaluation is the compile error that says so. Call the mark with
    the languages: `mark::only(welder::lang::py)`. */
void bare_mark_only_is_meaningless_call_it_with_languages();
} // namespace detail

/** The core decision a backend asks for each member: does @a member bind for
    language @a L under policy @a pol?

    @param member a reflection of the member to resolve.
    @param L      the target language.
    @param pol    the enclosing type's reflection policy.
    @return excluded ⇒ `false`; else an `only` mark ⇒ `true` iff it names @a L
            (under either policy — `only` is also the opt-in); else `automatic`
            ⇒ `true`; else (`opt_in`) ⇒ `true` iff explicitly included.
*/
consteval bool member_bound(std::meta::info member, lang L, policy_kind pol) {
    if (excluded_for(member, L))
        return false;
    auto onlys{std::meta::annotations_of_with_type(member, ^^detail::only_spec)};
    if (!onlys.empty()) {
        unsigned mask{0};
        for (auto a : onlys) {
            auto s{std::meta::extract<detail::only_spec>(a)};
            if (s.mask == 0)
                detail::bare_mark_only_is_meaningless_call_it_with_languages();
            mask |= s.mask;
        }
        return (mask & lang_bit(L)) != 0;
    }
    if (pol == policy_kind::automatic)
        return true;
    return included_for(member, L);
}

/** The types of the *public* base classes of @a type.

    Private/protected bases are an implementation detail and never participate in
    a binding. Backends decide how to treat a public base: typically, a base that
    is itself welded for the target language maps to native inheritance in that
    backend, while a non-welded base has its members flattened into the derived
    binding.

    @param type a reflection of the derived type.
    @return the reflected types of @a type's public direct bases, in declaration
            order.
*/
consteval std::vector<std::meta::info> public_bases(std::meta::info type) {
    std::vector<std::meta::info> out;
    constexpr auto ctx{std::meta::access_context::unchecked()};
    for (auto b : std::meta::bases_of(type, ctx))
        if (std::meta::is_public(b))
            out.push_back(std::meta::type_of(b));
    return out;
}

} // namespace welder
