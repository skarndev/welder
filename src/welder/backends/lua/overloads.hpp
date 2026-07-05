#pragma once
#include <array>
#include <cstddef>
#include <meta>
#include <vector>

#include <welder/bind_traits.hpp> // is_bindable_method / is_operator_candidate / …

/** @file
    Overload grouping shared by welder's Lua backends.

    Both Lua backends store a single entity per name — sol2 keeps one value per
    usertype-table key (and per metamethod slot), and the LuaCATS stub emits one
    documented `function` plus `---@overload` lines — so each must gather a name's
    several C++ overloads into one place. welder's generic driver, by contrast,
    visits overloads one at a time, which is what the Python backends want (their
    chained `pybind11`/`nanobind` `.def` registers each overload separately). These
    selectors bridge that gap: they gather a member's overload set from the scope
    where it is declared, reusing the *core* eligibility predicates
    (`<welder/bind_traits.hpp>`) so a group is exactly what the driver binds — the
    decision of what participates stays in the core.

    They are language-parameterised (every selector takes a `lang`) but live here,
    under `backends/lua/`, because only the Lua backends need name-gathering today;
    this keeps them out of the backend-agnostic core while letting the sol2 and
    LuaCATS backends stay separate. The layout mirrors `backends/python/doc_style.hpp`
    (shared by the Python backends).

    Requires the welder vocabulary first (via `import welder;` or `#include
    <welder/welder.hpp>`), like the rest of the reflection layer.

    Each scope is walked on its own, so a same-named member in a derived class still
    hides the base's (C++ name-hiding), the way the driver's flatten-then-overwrite
    already behaves.
*/

namespace welder::lua {

/** The bound method overloads sharing @a fn's name and static-ness, from the class
    where @a fn is declared, in declaration order.
    @param fn a reflection of one bindable method (see `detail::is_bindable_method`).
    @param L  the target language.
    @return the overload set (always contains @a fn). */
consteval std::vector<std::meta::info> method_overload_set(std::meta::info fn,
                                                           lang L) {
    const std::meta::info cls{std::meta::parent_of(fn)};
    const policy_kind pol{welder::policy_of(cls)};
    const auto name{std::meta::identifier_of(fn)};
    const bool is_static{std::meta::is_static_member(fn)};
    std::vector<std::meta::info> out{};
    for (auto m : std::meta::members_of(cls, std::meta::access_context::unchecked()))
        if (welder::detail::is_bindable_method(m, L, pol) &&
            std::meta::is_static_member(m) == is_static &&
            std::meta::identifier_of(m) == name)
            out.push_back(m);
    return out;
}

/** The bound operator overloads sharing @a fn's target slot (same operator and
    arity — hence the same special-method name), from @a fn's declaring class.
    @param fn a reflection of one operator candidate (see
              `detail::is_operator_candidate`).
    @param L  the target language.
    @return the overload set (always contains @a fn). */
consteval std::vector<std::meta::info> operator_overload_set(std::meta::info fn,
                                                             lang L) {
    const std::meta::info cls{std::meta::parent_of(fn)};
    const policy_kind pol{welder::policy_of(cls)};
    std::vector<std::meta::info> out{};
    for (auto m : std::meta::members_of(cls, std::meta::access_context::unchecked()))
        if (welder::detail::is_operator_candidate(m, L, pol) &&
            std::meta::operator_of(m) == std::meta::operator_of(fn) &&
            welder::detail::is_unary_operator(m) ==
                welder::detail::is_unary_operator(fn))
            out.push_back(m);
    return out;
}

/** The bound free-function overloads sharing @a fn's name, from @a fn's declaring
    namespace, in declaration order.
    @param fn a reflection of one bound namespace-scope function.
    @param L  the target language.
    @return the overload set (always contains @a fn). */
consteval std::vector<std::meta::info> function_overload_set(std::meta::info fn,
                                                             lang L) {
    const std::meta::info ns{std::meta::parent_of(fn)};
    const policy_kind pol{welder::policy_of(ns)};
    const auto name{std::meta::identifier_of(fn)};
    std::vector<std::meta::info> out{};
    for (auto m : std::meta::members_of(ns, std::meta::access_context::unchecked()))
        if (std::meta::is_function(m) && welder::welded_for(m, L) &&
            welder::member_bound(m, L, pol) &&
            std::meta::identifier_of(m) == name)
            out.push_back(m);
    return out;
}

/** The signature of an overload-set selector (`method_overload_set`, …). */
using overload_selector = std::vector<std::meta::info> (*)(std::meta::info, lang);

/** @a Select's overload set for @a Fn as a fixed-size, splice-ready static array.
    @tparam Select the selector (e.g. `method_overload_set`).
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
    set — the single visit on which a name-gathering backend emits the whole group.
    @tparam Select the selector.
    @param fn the candidate overload.
    @param L  the target language. */
template <overload_selector Select>
consteval bool is_overload_leader(std::meta::info fn, lang L) {
    auto v{Select(fn, L)};
    return !v.empty() && v.front() == fn;
}

} // namespace welder::lua
