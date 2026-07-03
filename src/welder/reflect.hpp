#pragma once
#include <meta>

/** @file
    Language-agnostic resolution: given a reflected type/member and a target
    language, decide what participates in binding. Backends consume these
    predicates and never re-implement the annotation semantics.

    @note This header depends on the welder vocabulary (`welder::weld_spec`,
    `lang`, …) but deliberately does NOT include `<welder/annotations.hpp>`: the
    vocabulary may instead arrive via `import welder;`, and including it textually
    as well would redeclare those entities. Provide the vocabulary first — either
    `import welder;` or `#include <welder/welder.hpp>` — then include this header.
*/

namespace welder {

/** Is @a type welded for language @a L — i.e. does it carry a matching `weld`
    annotation?

    @param type a reflection of the type to test.
    @param L    the target language.
    @return `true` iff @a type's `weld` annotation lists @a L.
*/
consteval bool welded_for(std::meta::info type, lang L) {
    auto anns{std::meta::annotations_of_with_type(type, ^^weld_spec)};
    return !anns.empty() &&
           (std::meta::extract<weld_spec>(anns[0]).mask & lang_bit(L)) != 0;
}

/** The reflection policy declared on @a type, defaulting to `automatic`.

    @param type a reflection of the type to inspect.
    @return the type's `policy` annotation, or `policy_kind::automatic` if none.
*/
consteval policy_kind policy_of(std::meta::info type) {
    auto anns{std::meta::annotations_of_with_type(type, ^^policy_spec)};
    return anns.empty() ? policy_kind::automatic
                        : std::meta::extract<policy_spec>(anns[0]).kind;
}

/** Does @a member carry an `exclude` mark covering language @a L?

    @param member a reflection of the member to test.
    @param L      the target language.
    @return `true` iff an `exclude` mark applies to @a L (a mark with mask `0`
            covers all languages).
*/
consteval bool excluded_for(std::meta::info member, lang L) {
    for (auto a : std::meta::annotations_of_with_type(member, ^^exclude_spec)) {
        auto s{std::meta::extract<exclude_spec>(a)};
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
    for (auto a : std::meta::annotations_of_with_type(member, ^^include_spec)) {
        auto s{std::meta::extract<include_spec>(a)};
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
    for (auto a : std::meta::annotations_of_with_type(member, ^^trust_bindable_spec)) {
        auto s{std::meta::extract<trust_bindable_spec>(a)};
        if (s.mask == 0 || (s.mask & lang_bit(L)) != 0)
            return true;
    }
    return false;
}

/** The core decision a backend asks for each member: does @a member bind for
    language @a L under policy @a pol?

    @param member a reflection of the member to resolve.
    @param L      the target language.
    @param pol    the enclosing type's reflection policy.
    @return excluded ⇒ `false`; else `automatic` ⇒ `true`; else (`opt_in`) ⇒
            `true` iff explicitly included.
*/
consteval bool member_bound(std::meta::info member, lang L, policy_kind pol) {
    if (excluded_for(member, L))
        return false;
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
