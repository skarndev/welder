#pragma once
#include <meta>

#include <welder/bind_traits.hpp> // is_unary_operator

#include <sol/sol.hpp>

/** @file
    The C++-operator → sol2/Lua metamethod map, factored out of the sol2 backend.

    Lua's metamethod model differs from Python's dunders, so this map lives beside
    the sol2 backend rather than in a shared Python header — the sol2 analogue of
    `<welder/backends/python/operators.hpp>`. The backend calls @ref
    welder::sol2::detail::operator_mm from its `special_method_name` (the `__name`
    that gates operator eligibility) and its `add_operator` (the `sol::meta_function`
    slot).

    Requires the welder vocabulary first (via `import welder;` or `#include
    <welder/welder.hpp>`) and `<sol/sol.hpp>`.
*/

namespace welder::sol2::detail {

/** A member operator's Lua metamethod, or `{…, nullptr}` if welder does not expose
    it. */
struct metamethod {
    ::sol::meta_function fn; /**< The sol2 metamethod slot. */
    const char* name;        /**< Its `__name`, or `nullptr` if not exposed. */
};

/** Map a member operator to its Lua metamethod (`{…, nullptr}` = not exposed).

    Unary vs binary is told apart by arity (a member operator takes 0 parameters
    when unary, 1 when binary), disambiguating the operators with both forms
    (`+`, `-`). The map reflects Lua's metamethod model, which differs from Python's
    dunders in three ways:
    - **No `__ne`/`__gt`/`__ge`.** Lua synthesizes `~=`, `>` and `>=` from `__eq`,
      `__lt` and `__le` (operands swapped), so `operator!=`, `operator>` and
      `operator>=` map to nothing — they already work in Lua once `==`/`<`/`<=` are
      bound.
    - **`^` is XOR, not power.** C++ `operator^` maps to Lua's bitwise-xor
      metamethod `__bxor`, not `__pow` (which is Lua's `^`).
    - **Bitwise metamethods are Lua ≥ 5.3 only** (`__band`, `__bor`, `__bxor`,
      `__bnot`, `__shl`, `__shr`) — absent on Lua 5.1 / LuaJIT — so they are
      `#if`-gated on `LUA_VERSION_NUM`.

    In-place compound assignments (`operator+=`, …), `<=>`, `&&`, `||`, `++`, `--`
    and `=` are not mapped (same as the Python backends).

    @param f a reflection of the operator function.
    @return the `sol::meta_function` and its `__name`, or a `nullptr` name.
*/
consteval metamethod operator_mm(std::meta::info f) {
    using std::meta::operators;
    using mf = ::sol::meta_function;
    const bool unary{welder::detail::is_unary_operator(f)};
    const metamethod none{mf::index, nullptr};
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:
            return unary ? none : metamethod{mf::addition, "__add"}; // no unary +
        case operators::op_minus:
            return unary ? metamethod{mf::unary_minus, "__unm"}
                         : metamethod{mf::subtraction, "__sub"};
        case operators::op_star:
            return unary ? none // unary * is dereference, not a Lua metamethod
                         : metamethod{mf::multiplication, "__mul"};
        case operators::op_slash:   return {mf::division, "__div"};
        case operators::op_percent: return {mf::modulus, "__mod"};
        case operators::op_equals_equals: return {mf::equal_to, "__eq"};
        case operators::op_less:          return {mf::less_than, "__lt"};
        case operators::op_less_equals:   return {mf::less_than_or_equal_to, "__le"};
        // Lua derives ~=, > and >= from __eq/__lt/__le, so these expose nothing.
        case operators::op_exclamation_equals:
        case operators::op_greater:
        case operators::op_greater_equals: return none;
        case operators::op_parentheses:    return {mf::call, "__call"};
        // operator[] -> Lua's __index (a sol2 fallback consulted after normal
        // member/method lookup, so it coexists with fields and methods).
        case operators::op_square_brackets: return {mf::index, "__index"};
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 503
        // Bitwise: Lua 5.3+ only. C++ operator^ is XOR (Lua __bxor), NOT power.
        case operators::op_caret:     return {mf::bitwise_xor, "__bxor"};
        case operators::op_tilde:
            return unary ? metamethod{mf::bitwise_not, "__bnot"} : none;
        case operators::op_ampersand:
            return unary ? none // unary & is address-of
                         : metamethod{mf::bitwise_and, "__band"};
        case operators::op_pipe:            return {mf::bitwise_or, "__bor"};
        case operators::op_less_less:       return {mf::bitwise_left_shift, "__shl"};
        case operators::op_greater_greater: return {mf::bitwise_right_shift, "__shr"};
#endif
        default: return none;
    }
}

} // namespace welder::sol2::detail
