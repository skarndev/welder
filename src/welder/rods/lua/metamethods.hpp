#pragma once
#include <meta>

#include <welder/bind_traits.hpp> // is_unary_operator

/** @file
    The C++-operator → Lua metamethod **name** map, shared by both Lua runtime rods
    (sol2 and LuaBridge3).

    Lua's metamethod model is one set of `__name` slots regardless of which C++
    wrapper lays the bindings down, so the *which operators are exposed and under
    what `__name`* decision lives here, once, and each rod reuses it:
    - the **sol2** rod (`rods/lua/sol2/metamethods.hpp`) sources the `__name` from
      @ref welder::rods::lua::lua_metamethod_name and pairs it with the matching
      `sol::meta_function` slot it registers through;
    - the **LuaBridge3** rod registers a metamethod *by its string name*
      (`addFunction("__add", …)`), so it uses this map directly.

    The map reflects Lua's metamethod model, which differs from Python's dunders in
    three ways:
    - **No `__ne`/`__gt`/`__ge`.** Lua synthesizes `~=`, `>` and `>=` from `__eq`,
      `__lt` and `__le` (operands swapped), so `operator!=`, `operator>` and
      `operator>=` map to nothing — they already work in Lua once `==`/`<`/`<=` are
      bound.
    - **`^` is XOR, not power.** C++ `operator^` maps to Lua's bitwise-xor
      metamethod `__bxor`, not `__pow` (which is Lua's `^`).
    - **Bitwise metamethods are Lua ≥ 5.3 only** (`__band`, `__bor`, `__bxor`,
      `__bnot`, `__shl`, `__shr`) — absent on Lua 5.1 / LuaJIT — so they are
      `#if`-gated on `LUA_VERSION_NUM`.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`) and
    the Lua headers (`<lua.h>`, pulled in by `<sol/sol.hpp>` /
    `<LuaBridge/LuaBridge.h>`) so `LUA_VERSION_NUM` is visible.
*/

namespace welder::inline v0::rods::lua {

/** Map a member operator to its Lua metamethod `__name`, or `nullptr` if welder
    does not expose it (which also gates operator eligibility in the driver).

    Unary vs binary is told apart by arity (a member operator takes 0 parameters
    when unary, 1 when binary), disambiguating the operators with both forms
    (`+`, `-`, `*`, `&`, `~`). In-place compound assignments (`operator+=`, …),
    `<=>`, `&&`, `||`, `++`, `--` and `=` are not mapped (same as the Python rods).

    @param f a reflection of the operator function.
    @return the metamethod `__name`, or `nullptr` when not exposed.
*/
consteval const char* lua_metamethod_name(std::meta::info f) {
    using std::meta::operators;
    const bool unary{::welder::detail::is_unary_operator(f)};
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:
            return unary ? nullptr : "__add"; // no unary +
        case operators::op_minus:
            return unary ? "__unm" : "__sub";
        case operators::op_star:
            return unary ? nullptr // unary * is dereference, not a Lua metamethod
                         : "__mul";
        case operators::op_slash:   return "__div";
        case operators::op_percent: return "__mod";
        case operators::op_equals_equals: return "__eq";
        case operators::op_less:          return "__lt";
        case operators::op_less_equals:   return "__le";
        // Lua derives ~=, > and >= from __eq/__lt/__le, so these expose nothing.
        case operators::op_exclamation_equals:
        case operators::op_greater:
        case operators::op_greater_equals: return nullptr;
        case operators::op_parentheses:    return "__call";
        // operator[] -> Lua's __index (a fallback consulted after normal
        // member/method lookup, so it coexists with fields and methods).
        case operators::op_square_brackets: return "__index";
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 503
        // Bitwise: Lua 5.3+ only. C++ operator^ is XOR (Lua __bxor), NOT power.
        case operators::op_caret:     return "__bxor";
        case operators::op_tilde:     return unary ? "__bnot" : nullptr;
        case operators::op_ampersand:
            return unary ? nullptr // unary & is address-of
                         : "__band";
        case operators::op_pipe:            return "__bor";
        case operators::op_less_less:       return "__shl";
        case operators::op_greater_greater: return "__shr";
#endif
        default: return nullptr;
    }
}

} // namespace welder::rods::lua