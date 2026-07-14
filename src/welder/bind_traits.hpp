#pragma once
#include <array>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <vector>

#include <welder/reflect.hpp> // resolution: welded_for / member_bound / public_bases

/** @file
    Backend-agnostic *selection* layer: the reflection predicates and selectors
    that decide **what** participates in a binding — which constructors, methods,
    operators, data members, namespace entities and base classes are eligible, and
    what their parameter names/types are.

    It answers "what", never "how": no backend API appears here, so every backend
    (pybind11, nanobind, sol2) shares this vocabulary and only supplies the emission
    primitives.

    @note Like `<welder/reflect.hpp>`, this depends on the welder vocabulary
    (`lang`, `policy_kind`, …) but does NOT include `<welder/annotations.hpp>`:
    provide the vocabulary first (`#include <welder/vocabulary.hpp>`), then this.
*/

namespace welder::inline v0::detail {

// --- function / parameter introspection -------------------------------------

/** A function's parameter *types*, as a static array of reflections.

    Usable as a non-type template argument so it can be spliced back into a pack.
    @tparam Fn a reflection of the function.
    @return an array of the parameter type reflections, in order.
*/
template <std::meta::info Fn>
consteval auto param_types() {
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    std::array<std::meta::info, n> types{};
    std::size_t i{0};
    for (auto p : std::meta::parameters_of(Fn))
        types[i++] = std::meta::type_of(p);
    return types;
}

/** A function's parameter *names*, in order.

    @tparam Fn a reflection of the function.
    @return an array of static-storage C strings, one per parameter, or `nullptr`
            for an unnamed one.
*/
template <std::meta::info Fn>
consteval auto param_names() {
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    std::array<const char*, n> names{};
    std::size_t i{0};
    for (auto p : std::meta::parameters_of(Fn))
        names[i++] = std::meta::has_identifier(p)
                         ? std::define_static_string(std::meta::identifier_of(p))
                         : nullptr;
    return names;
}

/** A `keep_alive` lifetime dependency: a (nurse, patient) index pair. */
struct keep_alive_pair {
    unsigned nurse;   /**< The keeper whose collection bounds the dependency. */
    unsigned patient; /**< The dependant kept alive until the nurse is collected. */
};

/** The `keep_alive` dependencies declared on @a Fn, in declaration order.

    Materialized as a fixed-size static array so a rod can splice each pair back as
    a `keep_alive<nurse, patient>()` call-policy template argument. Empty when @a Fn
    carries no `keep_alive` annotation.
    @tparam Fn a reflection of the callable.
    @return an array of the (nurse, patient) pairs. */
template <std::meta::info Fn>
consteval auto keep_alive_pairs() {
    constexpr std::size_t n{
        std::meta::annotations_of_with_type(Fn, ^^keep_alive_spec).size()};
    std::array<keep_alive_pair, n> out{};
    if constexpr (n != 0) {
        std::size_t i{0};
        for (auto a : std::meta::annotations_of_with_type(Fn, ^^keep_alive_spec)) {
            auto s{std::meta::extract<keep_alive_spec>(a)};
            out[i++] = {s.nurse, s.patient};
        }
    }
    return out;
}

/** Whether every parameter of @a Fn carries an identifier.

    Keyword-argument naming is all-or-nothing across backends, so an unnamed
    parameter means positional.
    @tparam Fn a reflection of the function.
    @return `true` iff all parameters are named.
*/
template <std::meta::info Fn>
consteval bool all_params_named() {
    for (auto p : std::meta::parameters_of(Fn))
        if (!std::meta::has_identifier(p))
            return false;
    return true;
}

// --- constructor / method / operator eligibility ----------------------------

/** A non-default, non-copy/move public constructor a backend should expose.

    The default constructor is handled separately (it may be implicit, hence not a
    member).
    @param c a reflection of the constructor.
    @return `true` iff @a c is public, non-deleted, not copy/move, and takes at
            least one parameter.
*/
consteval bool is_bindable_constructor(std::meta::info c) {
    return std::meta::is_constructor(c) && std::meta::is_public(c) &&
           !std::meta::is_deleted(c) && !std::meta::is_copy_constructor(c) &&
           !std::meta::is_move_constructor(c) &&
           !std::meta::parameters_of(c).empty();
}

/** The *shape* of a bindable method: a plain public member function.

    Shape only — whether it *participates* is the resolution's decision (its
    `class_member_participates` hook; `member_bound` under the shipped
    resolutions), composed by the carriage. Special members, destructors and
    operators are skipped (operators are classified separately).
    @param f a reflection of the member function.
    @return `true` iff @a f is a public, non-deleted, non-special member function.
*/
consteval bool is_method_candidate(std::meta::info f) {
    return std::meta::is_function(f) && !std::meta::is_constructor(f) &&
           !std::meta::is_special_member_function(f) &&
           !std::meta::is_destructor(f) && !std::meta::is_operator_function(f) &&
           std::meta::is_public(f) && !std::meta::is_deleted(f);
}

/** The *shape* of a bindable member operator.

    Shape only, like @ref is_method_candidate — participation is the resolution's
    call. Whether it maps to something in the target language — and under what
    name — is a backend decision (see `rod::special_method_name`).
    @param f a reflection of the operator function.
    @return `true` iff @a f is a public, non-deleted, non-special member operator.
*/
consteval bool is_operator_candidate(std::meta::info f) {
    return std::meta::is_function(f) && std::meta::is_operator_function(f) &&
           !std::meta::is_special_member_function(f) && std::meta::is_public(f) &&
           !std::meta::is_deleted(f);
}

/** Whether a member operator is unary (0 parameters) vs binary (1 parameter).

    Told apart by arity — this disambiguates the operators with both forms
    (`+`, `-`); backends use it to pick, e.g., `__neg__` vs `__sub__`.
    @param f a reflection of the operator function.
    @return `true` iff @a f is unary.
*/
consteval bool is_unary_operator(std::meta::info f) {
    return std::meta::parameters_of(f).empty();
}

// --- aggregate initialization -----------------------------------------------
//
// An aggregate (a simple POD-like struct: no user-declared constructors) cannot
// be constructed as T(a, b) — only brace-initialized T{a, b}. A backend may want
// to synthesize a field constructor so the target language can build it with
// values; these helpers decide when that is valid and expose the fields.

/** The fields an aggregate is initialized from: its non-static data members in
    declaration order (all public, by the aggregate rules).

    @tparam T the aggregate type.
    @return an array of the field reflections, in declaration order.
*/
template <class T>
consteval auto aggregate_fields() {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    constexpr std::size_t n{std::meta::nonstatic_data_members_of(^^T, ctx).size()};
    std::array<std::meta::info, n> fs{};
    std::size_t i{0};
    for (auto m : std::meta::nonstatic_data_members_of(^^T, ctx))
        fs[i++] = m;
    return fs;
}

/** Whether to synthesize an aggregate field constructor for @a T (language @a L,
    resolution @a Resolution).

    Only for a baseless aggregate with at least one field, *all* of which
    participate: a based aggregate's brace-init nests the base (a flat field ctor
    can't express it), and a partially-excluded one would leak an excluded field
    as a positional parameter (aggregate init is positional and all-or-nothing).
    An empty aggregate is already covered by the default constructor.
    @tparam T          the aggregate type.
    @tparam L          the target language.
    @tparam Resolution the carriage's resolution (its `class_member_participates`
                       decides whether each field binds).
    @return `true` iff a field constructor should be synthesized.
*/
template <class T, lang L, class Resolution>
consteval bool aggregate_initializable() {
    if (!std::is_aggregate_v<T> || !welder::public_bases(^^T).empty())
        return false;
    constexpr auto ctx{std::meta::access_context::unchecked()};
    auto fields{std::meta::nonstatic_data_members_of(^^T, ctx)};
    if (fields.empty())
        return false;
    const policy_kind pol{policy_of(^^T)};
    for (auto m : fields)
        if (!Resolution::class_member_participates(m, L, pol))
            return false;
    return true;
}

// --- namespace-member eligibility -------------------------------------------

/** The member kinds welder can expose from a namespace.

    `is_class_type`/`is_enum_type` throw on a non-type reflection, so they are
    reached only after `is_type`; the other predicates are total and safe on any
    reflection.
    @param mem a reflection of the namespace member.
    @return `true` iff @a mem is a class/enum type, a function, or a variable.
*/
consteval bool is_bindable_kind(std::meta::info mem) {
    return (std::meta::is_type(mem) &&
            (std::meta::is_class_type(mem) || std::meta::is_enum_type(mem))) ||
           std::meta::is_function(mem) || std::meta::is_variable(mem);
}

/** Whether a leaf entity binds: a welded candidate that also resolves as bound.

    @param mem a reflection of the namespace member.
    @param L   the target language.
    @param pol the namespace's policy.
    @return `true` iff @a mem is a bindable kind, welded for @a L, and bound under
            @a pol and its own marks.
*/
consteval bool entity_bound(std::meta::info mem, lang L, policy_kind pol) {
    return is_bindable_kind(mem) && welded_for(mem, L) && member_bound(mem, L, pol);
}

/** Whether @a ns holds anything that would bind, directly or nested.

    I.e. whether exposing it would yield a non-empty (sub)module. Each namespace
    contributes under its own policy; a nested namespace is recursed by the same
    rule as the dispatch (`member_bound` under the namespace's policy: automatic
    unless excluded, opt_in only if included).
    @param ns a reflection of the namespace.
    @param L  the target language.
    @return `true` iff some member (possibly nested) binds.
*/
consteval bool namespace_has_bound(std::meta::info ns, lang L) {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    const policy_kind pol{policy_of(ns)};
    for (auto mem : std::meta::members_of(ns, ctx)) {
        if (std::meta::is_namespace(mem)) {
            if (member_bound(mem, L, pol) && namespace_has_bound(mem, L))
                return true;
        } else if (entity_bound(mem, L, pol)) {
            return true;
        }
    }
    return false;
}

/** The **greedy** twin of namespace_has_bound: whether @a ns holds any *bindable
    kind* (ignoring the `weld` marker), directly or nested.

    Used by the tack-welding resolution to decide whether an unmarked namespace is
    worth turning into a submodule. Same shape as namespace_has_bound but with the
    `welded_for` gate dropped — a member counts if it is a bindable kind resolving as
    bound under the (marker-less) policy.
    @param ns a reflection of the namespace.
    @param L  the target language.
    @return `true` iff some member (possibly nested) is a bindable kind that binds.
*/
consteval bool namespace_has_bindable(std::meta::info ns, lang L) {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    const policy_kind pol{policy_of(ns)};
    for (auto mem : std::meta::members_of(ns, ctx)) {
        if (std::meta::is_namespace(mem)) {
            if (member_bound(mem, L, pol) && namespace_has_bindable(mem, L))
                return true;
        } else if (is_bindable_kind(mem) && member_bound(mem, L, pol)) {
            return true;
        }
    }
    return false;
}

// --- overload groups (resolution-aware) ---------------------------------------
//
// Several target frameworks store ONE entity per name — a Lua table key, a
// LuaCATS `function` declaration — so a name's C++ overloads must arrive as one
// group. The CARRIAGE owns that grouping (it visits a group's first participating
// overload, the *leader*, computes the whole set with these selectors and hands
// it to the rod's group hook): the rods never re-derive membership, so a group is
// exactly what the resolution admits — per-overload marks and bespoke
// (signature-level) resolutions stay consistent across every rod, and a mixed
// welded/unwelded set under one name can never split into clobbering halves.

/** The participating method overloads sharing @a fn's name and static-ness, from
    the class where @a fn is declared, in declaration order.
    @tparam Resolution the carriage's resolution.
    @param fn a reflection of one participating method (see is_method_candidate).
    @param L  the target language.
    @return the overload set (always contains @a fn). */
template <class Resolution>
consteval std::vector<std::meta::info> method_overload_set(std::meta::info fn,
                                                           lang L) {
    const std::meta::info cls{std::meta::parent_of(fn)};
    const policy_kind pol{::welder::policy_of(cls)};
    const auto name{std::meta::identifier_of(fn)};
    const bool is_static{std::meta::is_static_member(fn)};
    std::vector<std::meta::info> out{};
    for (auto m : std::meta::members_of(cls, std::meta::access_context::unchecked()))
        if (is_method_candidate(m) && Resolution::class_member_participates(m, L, pol) &&
            std::meta::is_static_member(m) == is_static &&
            std::meta::identifier_of(m) == name)
            out.push_back(m);
    return out;
}

/** The participating operator overloads sharing @a fn's target slot (same
    operator and arity — hence the same special-method name), from @a fn's
    declaring class.
    @tparam Resolution the carriage's resolution.
    @param fn a reflection of one participating operator (see is_operator_candidate).
    @param L  the target language.
    @return the overload set (always contains @a fn). */
template <class Resolution>
consteval std::vector<std::meta::info> operator_overload_set(std::meta::info fn,
                                                             lang L) {
    const std::meta::info cls{std::meta::parent_of(fn)};
    const policy_kind pol{::welder::policy_of(cls)};
    std::vector<std::meta::info> out{};
    for (auto m : std::meta::members_of(cls, std::meta::access_context::unchecked()))
        if (is_operator_candidate(m) && Resolution::class_member_participates(m, L, pol) &&
            std::meta::operator_of(m) == std::meta::operator_of(fn) &&
            is_unary_operator(m) == is_unary_operator(fn))
            out.push_back(m);
    return out;
}

/** The participating free-function overloads sharing @a fn's name, from @a fn's
    declaring namespace, in declaration order. Membership is the resolution's
    namespace-member verdict, so the set is exactly what the namespace walk binds.
    @tparam Resolution the carriage's resolution.
    @param fn a reflection of one participating namespace-scope function.
    @param L  the target language.
    @return the overload set (always contains @a fn). */
template <class Resolution>
consteval std::vector<std::meta::info> function_overload_set(std::meta::info fn,
                                                             lang L) {
    const std::meta::info ns{std::meta::parent_of(fn)};
    const policy_kind pol{::welder::policy_of(ns)};
    const auto name{std::meta::identifier_of(fn)};
    std::vector<std::meta::info> out{};
    for (auto m : std::meta::members_of(ns, std::meta::access_context::unchecked()))
        if (std::meta::is_function(m) && Resolution::member_participates(m, L, pol) &&
            std::meta::identifier_of(m) == name)
            out.push_back(m);
    return out;
}

/** The signature of an overload-set selector specialization. */
using overload_selector = std::vector<std::meta::info> (*)(std::meta::info, lang);

/** @a Select's overload set for @a Fn as a fixed-size, splice-ready static array.
    @tparam Select the selector specialization (e.g. `method_overload_set<R>`).
    @tparam Fn     the representative overload.
    @tparam L      the target language.
    @return an array of the group's member reflections, in declaration order. */
template <overload_selector Select, std::meta::info Fn, lang L>
consteval auto overload_group() {
    constexpr std::size_t n{Select(Fn, L).size()};
    std::array<std::meta::info, n> out{};
    // Guard the fill: std::array<T, 0>::operator[] is not usable (n is >= 1 for a
    // group leader, but the guard keeps this well-formed regardless).
    if constexpr (n != 0) {
        auto v{Select(Fn, L)};
        for (std::size_t i{0}; i < n; ++i)
            out[i] = v[i];
    }
    return out;
}

/** Whether @a fn is the first (declaration order) member of its @a Select overload
    set — the single visit on which the carriage emits the whole group.
    @tparam Select the selector specialization.
    @param fn the candidate overload.
    @param L  the target language. */
template <overload_selector Select>
consteval bool is_overload_leader(std::meta::info fn, lang L) {
    auto v{Select(fn, L)};
    return !v.empty() && v.front() == fn;
}

/** The semi-manual (`weld_function<Fn>`) group: @a Fn first — the user's named
    entity resolves the group's target name — then @a Fn's participating siblings.

    Gathering the siblings keeps `weld_function` consistent with the namespace
    walk (and keeps one-value-per-name frameworks from silently clobbering): one
    call welds the name's participating overload set. @a Fn itself is included
    even when its own marks would resolve it out — the explicit call is the
    stronger statement of intent.
    An identifier-less @a Fn (a template instantiation formed with `substitute`)
    has no name for siblings to share, so its group is just itself.
    @tparam Resolution the carriage's resolution.
    @tparam Fn         the explicitly welded function.
    @tparam L          the target language.
    @return the group as a static array, @a Fn first. */
template <class Resolution, std::meta::info Fn, lang L>
consteval auto manual_function_group() {
    if constexpr (!std::meta::has_identifier(Fn)) {
        return std::array<std::meta::info, 1>{Fn};
    } else {
        constexpr std::size_t n{[] {
            auto v{function_overload_set<Resolution>(Fn, L)};
            std::size_t extra{1};
            for (auto m : v)
                if (m == Fn)
                    extra = 0;
            return v.size() + extra;
        }()};
        std::array<std::meta::info, n> out{};
        out[0] = Fn;
        std::size_t i{1};
        for (auto m : function_overload_set<Resolution>(Fn, L))
            if (m != Fn)
                out[i++] = m;
        return out;
    }
}

/** The participating constructors of @a Type under policy @a Pol: every
    bindable-shape constructor (see is_bindable_constructor) the resolution
    admits, in declaration order.

    Constructors resolve **symmetrically** with every other member — the type's
    policy and the constructor's own marks decide, per constructor (`opt_in`
    binds only marked-`include` constructors). The carriage guards the silent
    consequence: filtering that leaves a type with *no* constructor at all is a
    hard error unless the emptiness is explicit (see bind_type's
    no-constructor-left static_assert), which is why @a Pol is a parameter — the
    guard probes the same resolution under `policy_kind::automatic` as its
    baseline.

    The default constructor is decided separately (it may be implicit, hence not
    a member — see @ref default_ctor_admitted), as is the synthesized aggregate
    constructor.
    @tparam Resolution the carriage's resolution.
    @tparam Type       the class type reflection.
    @tparam L          the target language.
    @tparam Pol        the policy to resolve under (the type's own, or
                       `automatic` for the guard's baseline).
    @return the constructor reflections as a static array. */
template <class Resolution, std::meta::info Type, lang L, policy_kind Pol>
consteval auto ctor_group() {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    constexpr std::size_t n{[] {
        std::size_t count{0};
        for (auto c : std::meta::members_of(Type, ctx))
            if (is_bindable_constructor(c) &&
                Resolution::class_member_participates(c, L, Pol))
                ++count;
        return count;
    }()};
    std::array<std::meta::info, n> out{};
    if constexpr (n != 0) {
        std::size_t i{0};
        for (auto c : std::meta::members_of(Type, ctx))
            if (is_bindable_constructor(c) &&
                Resolution::class_member_participates(c, L, Pol))
                out[i++] = c;
    }
    return out;
}

/** Whether the resolution admits @a Type's DEFAULT constructor.

    The default constructor may be *implicit* — no declaration, so no marks and
    nothing for `opt_in` to filter: it is admitted whenever the type is
    default-constructible (the carriage checks constructibility itself, against
    the rod's construction type). A *declared* default constructor is consulted
    under `policy_kind::automatic` — its explicit marks are honored (so
    `[[=welder::mark::exclude]] T() = default;` suppresses default construction),
    but `opt_in`'s default-out is not: you cannot `include` an implicit
    constructor, so filtering the declared spelling by default would make
    `T() = default;` (a C++ no-op) silently change the binding.
    @tparam Resolution the carriage's resolution.
    @tparam Type       the class type reflection.
    @tparam L          the target language. */
template <class Resolution, std::meta::info Type, lang L>
consteval bool default_ctor_admitted() {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    for (auto c : std::meta::members_of(Type, ctx))
        if (std::meta::is_constructor(c) && std::meta::parameters_of(c).empty())
            return std::meta::is_public(c) && !std::meta::is_deleted(c) &&
                   Resolution::class_member_participates(c, L,
                                                         policy_kind::automatic);
    return true; // implicit (or absent): constructibility alone decides
}

// --- inheritance: native bases ----------------------------------------------
//
// `weld` marks a type as an independently-registered, module-discoverable entity,
// NOT as an inheritance directive. The most-derived type's `weld` drives which
// languages bind; a base need not be welded to contribute its members. Two kinds
// of public base fall out for a binding:
//   * a welded base -> registered as its own target-language class, linked via
//     native inheritance (passed as a base of the derived class handle).
//   * a non-welded base -> a plain C++ mixin with no standalone type, whose
//     eligible members are flattened onto the derived binding.

/** Collect the native bases of @a type for @a L: its nearest welded ancestors.

    Found by looking *past* non-welded bases (whose members are flattened instead),
    so a welded base reachable only through a non-welded one is still linked. A
    virtual diamond can reach the same welded base by several paths, so the list is
    deduplicated.
    @param type a reflection of the derived type.
    @param L    the target language.
    @param[out] out accumulates the deduplicated native base reflections.
*/
consteval void collect_native_bases(std::meta::info type, lang L,
                                    std::vector<std::meta::info>& out) {
    for (auto base : welder::public_bases(type)) {
        if (welder::welded_for(base, L)) {
            bool seen{false};
            for (auto e : out)
                if (e == base) {
                    seen = true;
                    break;
                }
            if (!seen)
                out.push_back(base);
        } else {
            collect_native_bases(base, L, out); // descend through the mixin
        }
    }
}

/** The native bases of @a Type for @a L as a static array of type reflections.

    Usable as a non-type template argument (spliced into the backend's class handle
    template). Same set as collect_native_bases().
    @tparam Type the derived type reflection.
    @tparam L    the target language.
    @return an array of the native base type reflections.
*/
template <std::meta::info Type, lang L>
consteval auto native_base_types() {
    constexpr std::size_t n{[] {
        std::vector<std::meta::info> v;
        collect_native_bases(Type, L, v);
        return v.size();
    }()};
    std::array<std::meta::info, n> types{};
    // Guard the fill: std::array<T, 0>::operator[] is not consteval, so it must
    // not be instantiated when a type has no native bases (the common case).
    if constexpr (n != 0) {
        std::vector<std::meta::info> v;
        collect_native_bases(Type, L, v);
        std::size_t i{0};
        for (auto base : v)
            types[i++] = base;
    }
    return types;
}

} // namespace welder::detail
