#pragma once
#include <meta>
#include <string> // accessor_explicit_name

#include <welder/diag.hpp>

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

/** Is @a mem a namespace-scope alias naming a class-template specialization —
    the one way an *instantiation* can enter a namespace sweep?

    `members_of(ns)` enumerates the class *template*, never its instantiations; a
    namespace-scope `using IntRing = Ring<int>;` is therefore welder's vehicle for
    welding one: the alias supplies both the target-language name (its identifier)
    and — for text-emitting rods — the C++ spelling of the otherwise unnameable
    specialization (`has_identifier` is `false` on `Ring<int>` itself).
    @param mem a reflection of a namespace member.
    @return `true` iff @a mem is a type alias whose target is a class-template
            specialization. */
consteval bool names_template_specialization(std::meta::info mem) {
    return std::meta::is_type_alias(mem) &&
           std::meta::is_class_type(std::meta::dealias(mem)) &&
           std::meta::has_template_arguments(std::meta::dealias(mem));
}

/** Is the specialization named by alias @a mem welded for language @a L — reading
    the alias's own `weld` first, the instantiation's (via its template) second?

    A `weld` on the alias-declaration **takes precedence** over the template's: it
    is the more specific declaration, and the opt-in for a third-party template you
    cannot annotate (`using VBuf [[=welder::weld(welder::lang::py)]] =
    vendor::Buf<int>;`). With no alias-level `weld`, the mark on the class template
    is read through the instantiation as usual.
    @param mem a reflection of the alias (see @ref names_template_specialization).
    @param L   the target language.
    @return `true` iff the aliased specialization is welded for @a L. */
consteval bool alias_welded_for(std::meta::info mem, lang L) {
    auto own{std::meta::annotations_of_with_type(mem, ^^detail::weld_spec)};
    if (!own.empty())
        return (std::meta::extract<detail::weld_spec>(own[0]).mask & lang_bit(L)) !=
               0;
    return welded_for(std::meta::dealias(mem), L);
}

/** May the annotations on alias-declaration @a mem appear there?

    Only `weld` and `weld_as` are meaningful on a namespace-scope alias (they
    override the template's); every other welder mark — `policy`, `exclude` /
    `include` / `only`, `trust_bindable`, `doc` / `returns` / `tparam`,
    `return_policy`, `keep_alive` — belongs on the class template itself, where it
    applies to *all* instantiations, and is diagnosed here so it cannot be silently
    ignored. Non-welder annotations are not welder's business and pass.
    @param mem a reflection of the alias to check.
    @return `true` iff @a mem carries no welder annotation besides `weld`/`weld_as`. */
consteval bool alias_marks_admissible(std::meta::info mem) {
    for (auto a : std::meta::annotations_of(mem)) {
        auto t{std::meta::type_of(a)};
        if (t == ^^detail::policy_spec || t == ^^detail::weld_protected_spec ||
            t == ^^detail::exclude_spec || t == ^^detail::include_spec ||
            t == ^^detail::only_spec || t == ^^detail::trust_bindable_spec ||
            t == ^^detail::return_policy_spec || t == ^^detail::keep_alive_spec ||
            t == ^^detail::accessor_spec)
            return false;
        if (std::meta::has_template_arguments(t)) {
            auto tmpl{std::meta::template_of(t)};
            if (tmpl == ^^detail::doc_spec || tmpl == ^^detail::return_doc_spec ||
                tmpl == ^^detail::tparam_spec)
                return false;
        }
    }
    return true;
}

/** May the annotations on **member** type-alias @a mem appear there?

    A member alias participates by the *member* rules (the outer's policy plus
    the alias's own marks) with the bindability gate as the register-or-skip
    arbiter — so `weld_as` and the participation marks (`exclude` / `include` /
    `only`) are meaningful on it. Everything else is not: `weld` (participation
    never reads it — nested registration follows the outer), `policy` /
    `weld_protected` (they belong on the *target* type), `trust_bindable` /
    `return_policy` / `keep_alive` / `doc` / `returns` / `tparam` (no surface
    they could apply to). Those are diagnosed rather than silently ignored.
    Non-welder annotations are not welder's business and pass.
    @param mem a reflection of the member alias to check.
    @return `true` iff @a mem carries only admissible welder annotations. */
consteval bool member_alias_marks_admissible(std::meta::info mem) {
    for (auto a : std::meta::annotations_of(mem)) {
        auto t{std::meta::type_of(a)};
        if (t == ^^detail::weld_spec || t == ^^detail::policy_spec ||
            t == ^^detail::weld_protected_spec ||
            t == ^^detail::trust_bindable_spec ||
            t == ^^detail::return_policy_spec || t == ^^detail::keep_alive_spec ||
            t == ^^detail::accessor_spec)
            return false;
        if (std::meta::has_template_arguments(t)) {
            auto tmpl{std::meta::template_of(t)};
            if (tmpl == ^^detail::doc_spec || tmpl == ^^detail::return_doc_spec ||
                tmpl == ^^detail::tparam_spec)
                return false;
        }
    }
    return true;
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

/** Does @a type admit its *protected* members for language @a L — i.e. does it
    carry a `policy::weld_protected` annotation covering @a L?

    Read off the member's *declaring* class (so a flattened non-welded base
    admits its own protected members, consistent with how its marks and policy
    resolve), and — like every annotation — read through a class-template
    instantiation from the template's declaration. Repeated annotations union.
    This is only the *default* arbitration: a resolution with a
    `protected_participates` hook replaces it (see the carriage's
    `member_access_admitted`). Private members are never admitted, under any
    resolution.

    @param type a reflection of the declaring class.
    @param L    the target language.
    @return `true` iff a `weld_protected` annotation applies to @a L (mask `0`
            covers all languages).
*/
consteval bool protected_welded(std::meta::info type, lang L) {
    for (auto a : std::meta::annotations_of_with_type(
             type, ^^detail::weld_protected_spec)) {
        auto s{std::meta::extract<detail::weld_protected_spec>(a)};
        if (s.mask == 0 || (s.mask & lang_bit(L)) != 0)
            return true;
    }
    return false;
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

/** Is data member @a member bound **read-only** for @a L by a `no_reassign` mark?

    `no_reassign` forces the read-only binding on a mutable member (see
    detail::no_reassign_spec) — the `const`-member binding, without the `const`. A
    rod's `add_field` ORs this with the member's const-ness to choose the read-only
    path, so an in-place mutation still writes through but a rebind is rejected.
    @param member a reflection of the data member to test.
    @param L      the target language.
    @return `true` iff a `no_reassign` mark covers @a L (mask `0` covers all
            languages). */
consteval bool member_no_reassign(std::meta::info member, lang L) {
    for (auto a : std::meta::annotations_of_with_type(member, ^^detail::no_reassign_spec)) {
        auto s{std::meta::extract<detail::no_reassign_spec>(a)};
        if (s.mask == 0 || (s.mask & lang_bit(L)) != 0)
            return true;
    }
    return false;
}

/** Does @a entity carry any `no_reassign` mark at all (any language)?

    `no_reassign` shapes how a *nonstatic data member* binds (read-only), so it is
    meaningless on anything else. The carriage uses this to diagnose the mark on a
    function, a type, a static member, or a namespace-scope variable — a hard error
    rather than a silent no-op, mirroring `has_accessor_mark`.
    @param entity a reflection of the entity to test.
    @return `true` iff a `no_reassign` mark is present. */
consteval bool has_no_reassign_mark(std::meta::info entity) {
    return !std::meta::annotations_of_with_type(entity, ^^detail::no_reassign_spec)
                .empty();
}

/** Does @a member carry a `getter`/`setter` mark of @a role covering @a L?

    The stored @ref detail::accessor_spec is deliberately non-templated (see
    `accessor_name_capacity`), so this reads off a *dynamic* reflection — usable
    inside `member_bound` and the overload-set selectors, where the member is a
    plain value.
    @param member a reflection of the member function to test.
    @param role   which property half to look for.
    @param L      the target language.
    @return `true` iff a mark of @a role applies to @a L (mask `0` covers all
            languages). */
consteval bool accessor_marked(std::meta::info member, accessor_role role, lang L) {
    for (auto a : std::meta::annotations_of_with_type(member, ^^detail::accessor_spec)) {
        auto s{std::meta::extract<detail::accessor_spec>(a)};
        if (s.role == role && (s.mask == 0 || (s.mask & lang_bit(L)) != 0))
            return true;
    }
    return false;
}

/** Does @a member supply either property half for language @a L — i.e. is it an
    accessor the property machinery claims (and the method sweep must skip)?
    @param member a reflection of the member function to test.
    @param L      the target language.
    @return `true` iff a `getter` or `setter` mark covers @a L. */
consteval bool is_accessor_for(std::meta::info member, lang L) {
    return accessor_marked(member, accessor_role::getter, L) ||
           accessor_marked(member, accessor_role::setter, L);
}

/** Does @a member carry any `getter`/`setter` mark at all (any role, any
    language)? The namespace walk uses this to diagnose accessor marks on free
    functions, where no property surface exists.
    @param member a reflection of the entity to test.
    @return `true` iff an accessor mark is present. */
consteval bool has_accessor_mark(std::meta::info member) {
    return !std::meta::annotations_of_with_type(member, ^^detail::accessor_spec)
                .empty();
}

/** The explicit property name an accessor mark of @a role forces for @a L, or
    `""` when the name derives from the function's identifier.

    The first covering mark with a non-empty name wins (repeat the annotation
    for a different name per language, like `weld_as`).
    @param member a reflection of the accessor function.
    @param role   which property half to read.
    @param L      the target language.
    @return the explicit name (verbatim), or an empty string. */
consteval std::string accessor_explicit_name(std::meta::info member,
                                             accessor_role role, lang L) {
    for (auto a : std::meta::annotations_of_with_type(member, ^^detail::accessor_spec)) {
        auto s{std::meta::extract<detail::accessor_spec>(a)};
        if (s.role == role && (s.mask == 0 || (s.mask & lang_bit(L)) != 0) &&
            s.name[0] != '\0')
            return std::string{s.name};
    }
    return {};
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

/** Reject a `return_policy` on @a Fn (for language @a L) that contradicts @a Fn's
    return type.

    A reference-category policy (`reference` / `reference_internal`) promises the
    target a view into a live C++ object; on a by-value (prvalue) return there is
    no such object — only a temporary — so the view would dangle. This is a
    contradiction in *any* language, so every rod (Python and Lua alike) runs the
    check at its per-overload bind site. Anything the framework could plausibly
    honor passes; only the dangling case is diagnosed.

    @tparam Fn a reflection of the callable being bound.
    @tparam L  the target language.
    @throws diag::dangling_return_policy (a constant-evaluation error) on the
            contradiction. */
template <std::meta::info Fn, lang L>
consteval void validate_return_policy() {
    constexpr rv_kind k{return_policy_of(Fn, L)};
    if constexpr (k == rv_kind::reference || k == rv_kind::reference_internal) {
        constexpr std::meta::info rt{std::meta::return_type_of(Fn)};
        if constexpr (!(std::meta::is_pointer_type(rt) ||
                        std::meta::is_lvalue_reference_type(rt) ||
                        std::meta::is_rvalue_reference_type(rt)))
            throw diag::dangling_return_policy{};
    }
}

/** The core decision a backend asks for each member: does @a member bind for
    language @a L under policy @a pol?

    @param member a reflection of the member to resolve.
    @param L      the target language.
    @param pol    the enclosing type's reflection policy.
    @return excluded ⇒ `false`; else an `only` mark ⇒ `true` iff it names @a L
            (under either policy — `only` is also the opt-in); else `automatic`
            ⇒ `true`; else (`opt_in`) ⇒ `true` iff explicitly included — where a
            `getter`/`setter` mark covering @a L also counts as the opt-in
            (marking an accessor is an unambiguous statement of intent, like
            `only`).
    @throws diag::bare_mark_only (a constant-evaluation error) on an uncalled
            `[[=welder::mark::only]]` — it must name the languages.
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
                throw diag::bare_mark_only{};
            mask |= s.mask;
        }
        return (mask & lang_bit(L)) != 0;
    }
    if (pol == policy_kind::automatic)
        return true;
    return included_for(member, L) || is_accessor_for(member, L);
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
