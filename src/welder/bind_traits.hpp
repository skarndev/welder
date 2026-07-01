#pragma once
#include <array>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <vector>

#include <welder/reflect.hpp> // resolution: welded_for / member_bound / public_bases

// Backend-agnostic *selection* layer: the reflection predicates and selectors
// that decide **what** participates in a binding — which constructors, methods,
// operators, data members, namespace entities and base classes are eligible, and
// what their parameter names/types are. It answers "what", never "how": no
// backend API appears here, so every backend (pybind11 today; nanobind / lua
// later) shares this vocabulary and only supplies the emission primitives.
//
// Like <welder/reflect.hpp>, this depends on the welder vocabulary (lang,
// policy_kind, ...) but does NOT include <welder/annotations.hpp>: provide the
// vocabulary first (`import welder;` or `#include <welder/welder.hpp>`), then this.

namespace welder::detail {

// --- function / parameter introspection -------------------------------------

// A function's parameter *types*, as a static array of reflections (usable as a
// non-type template argument so it can be spliced back into a pack).
template <std::meta::info Fn>
consteval auto param_types() {
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    std::array<std::meta::info, n> types{};
    std::size_t i{0};
    for (auto p : std::meta::parameters_of(Fn))
        types[i++] = std::meta::type_of(p);
    return types;
}

// A function's parameter *names*, in order — a static-storage C string per
// parameter, or nullptr for an unnamed one.
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

// Whether every parameter of Fn carries an identifier. Keyword-argument naming
// is all-or-nothing across backends, so an unnamed parameter means positional.
template <std::meta::info Fn>
consteval bool all_params_named() {
    for (auto p : std::meta::parameters_of(Fn))
        if (!std::meta::has_identifier(p))
            return false;
    return true;
}

// --- constructor / method / operator eligibility ----------------------------

// A non-default, non-copy/move public constructor a backend should expose. The
// default constructor is handled separately (it may be implicit, hence not a
// member).
consteval bool is_bindable_constructor(std::meta::info c) {
    return std::meta::is_constructor(c) && std::meta::is_public(c) &&
           !std::meta::is_deleted(c) && !std::meta::is_copy_constructor(c) &&
           !std::meta::is_move_constructor(c) &&
           !std::meta::parameters_of(c).empty();
}

// A member function a backend should expose as a method, honoring the same
// exclude/include/policy resolution as data members. Special members,
// destructors and operators are skipped (operators are classified separately).
consteval bool is_bindable_method(std::meta::info f, lang L, policy_kind pol) {
    return std::meta::is_function(f) && !std::meta::is_constructor(f) &&
           !std::meta::is_special_member_function(f) &&
           !std::meta::is_destructor(f) && !std::meta::is_operator_function(f) &&
           std::meta::is_public(f) && !std::meta::is_deleted(f) &&
           member_bound(f, L, pol);
}

// A member operator that *resolves* as bound (public, non-deleted, not a special
// member, member_bound). Whether it maps to something in the target language —
// and under what name — is a backend decision (see backend::special_method_name);
// this is the language-agnostic half of the test.
consteval bool is_operator_candidate(std::meta::info f, lang L, policy_kind pol) {
    return std::meta::is_function(f) && std::meta::is_operator_function(f) &&
           !std::meta::is_special_member_function(f) && std::meta::is_public(f) &&
           !std::meta::is_deleted(f) && member_bound(f, L, pol);
}

// Whether a member operator is unary (0 parameters) vs binary (1 parameter),
// told apart by arity — this disambiguates the operators with both forms (+, -).
// Backends use it to pick, e.g., __neg__ vs __sub__.
consteval bool is_unary_operator(std::meta::info f) {
    return std::meta::parameters_of(f).empty();
}

// --- aggregate initialization -----------------------------------------------
//
// An aggregate (a simple POD-like struct: no user-declared constructors) cannot
// be constructed as T(a, b) — only brace-initialized T{a, b}. A backend may want
// to synthesize a field constructor so the target language can build it with
// values; these helpers decide when that is valid and expose the fields.

// The fields an aggregate is initialized from: its non-static data members in
// declaration order (all public, by the aggregate rules).
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

// Whether to synthesize an aggregate field constructor for T (language L). Only
// for a baseless aggregate with at least one field, *all* of which bind: a based
// aggregate's brace-init nests the base (a flat field ctor can't express it), and
// a partially-excluded one would leak an excluded field as a positional parameter
// (aggregate init is positional and all-or-nothing). An empty aggregate is already
// covered by the default constructor.
template <class T, lang L>
consteval bool aggregate_initializable() {
    if (!std::is_aggregate_v<T> || !welder::public_bases(^^T).empty())
        return false;
    constexpr auto ctx{std::meta::access_context::unchecked()};
    auto fields{std::meta::nonstatic_data_members_of(^^T, ctx)};
    if (fields.empty())
        return false;
    const policy_kind pol{policy_of(^^T)};
    for (auto m : fields)
        if (!member_bound(m, L, pol))
            return false;
    return true;
}

// --- namespace-member eligibility -------------------------------------------

// The member kinds welder can expose from a namespace. is_class_type/is_enum_type
// throw on a non-type reflection, so they are reached only after is_type; the other
// predicates are total and safe on any reflection.
consteval bool is_bindable_kind(std::meta::info mem) {
    return (std::meta::is_type(mem) &&
            (std::meta::is_class_type(mem) || std::meta::is_enum_type(mem))) ||
           std::meta::is_function(mem) || std::meta::is_variable(mem);
}

// A leaf entity binds iff it is a welded candidate that also resolves as bound
// under namespace policy `pol` and its own marks.
consteval bool entity_bound(std::meta::info mem, lang L, policy_kind pol) {
    return is_bindable_kind(mem) && welded_for(mem, L) && member_bound(mem, L, pol);
}

// Whether `ns` holds anything that would bind, directly or nested — i.e. whether
// exposing it would yield a non-empty (sub)module. Each namespace contributes
// under its own policy; a nested namespace is recursed by the same rule as the
// dispatch (member_bound under ns's policy: automatic unless excluded, opt_in
// only if included).
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

// The native bases of Type for L: its nearest welded ancestors, found by looking
// *past* non-welded bases (whose members are flattened instead). So a welded base
// reachable only through a non-welded one is still linked. A virtual diamond can
// reach the same welded base by several paths, so the list is deduplicated.
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

// The same set as a static array of type reflections usable as a non-type
// template argument (spliced into the backend's class handle template).
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
