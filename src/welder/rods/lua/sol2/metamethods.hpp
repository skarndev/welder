#pragma once
#include <meta>

#include <welder/bind_traits.hpp>          // is_unary_operator
#include <welder/rods/lua/metamethods.hpp> // shared operator -> Lua __name map

#include <sol/sol.hpp>

/** @file
    The C++-operator → sol2 metamethod map, factored out of the sol2 backend.

    Which operators Lua exposes, and under what `__name`, is shared with the
    LuaBridge3 rod in @ref welder::rods::lua::lua_metamethod_name (Lua's metamethod
    model is one set of slots, independent of the C++ wrapper). This header adds only
    the sol2-specific half: the `sol::meta_function` *slot* each `__name` registers
    through. The backend calls @ref ::welder::rods::sol2::operator_mm from its
    `special_method_name` (the `__name`, sourced from the shared map, that gates
    operator eligibility) and its `add_operator` (the `sol::meta_function` slot).

    Requires the welder vocabulary first (via `import welder;` or `#include
    <welder/vocabulary.hpp>`) and `<sol/sol.hpp>`.
*/

namespace welder::rods::sol2 {

/** A member operator's sol2 metamethod, or `{…, nullptr}` if welder does not expose
    it. */
struct metamethod {
    ::sol::meta_function fn; /**< The sol2 metamethod slot. */
    const char* name;        /**< Its `__name`, or `nullptr` if not exposed. */
};

/** Map a member operator to its sol2 metamethod (`{…, nullptr}` = not exposed).

    The `__name` and *which* operators are exposed come from the shared
    @ref welder::rods::lua::lua_metamethod_name (the same set the LuaBridge3 rod
    uses); this only adds the sol2 `sol::meta_function` slot for each. `nullptr`
    name ⇒ not exposed (also gates operator eligibility in the driver). Unary vs
    binary is told apart by arity, disambiguating the operators with both forms
    (`+`, `-`, `*`, `&`, `~`).

    @param f a reflection of the operator function.
    @return the `sol::meta_function` and its `__name`, or a `nullptr` name.
*/
consteval metamethod operator_mm(std::meta::info f) {
    using std::meta::operators;
    using mf = ::sol::meta_function;
    const char* name{::welder::rods::lua::lua_metamethod_name(f)};
    if (name == nullptr)
        return {mf::index, nullptr};
    const bool unary{::welder::detail::is_unary_operator(f)};
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:          return {mf::addition, name};
        case operators::op_minus:
            return unary ? metamethod{mf::unary_minus, name}
                         : metamethod{mf::subtraction, name};
        case operators::op_star:          return {mf::multiplication, name};
        case operators::op_slash:         return {mf::division, name};
        case operators::op_percent:       return {mf::modulus, name};
        case operators::op_equals_equals: return {mf::equal_to, name};
        case operators::op_less:          return {mf::less_than, name};
        case operators::op_less_equals:   return {mf::less_than_or_equal_to, name};
        case operators::op_parentheses:   return {mf::call, name};
        case operators::op_square_brackets: return {mf::index, name};
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 503
        case operators::op_caret:         return {mf::bitwise_xor, name};
        case operators::op_tilde:         return {mf::bitwise_not, name};
        case operators::op_ampersand:     return {mf::bitwise_and, name};
        case operators::op_pipe:          return {mf::bitwise_or, name};
        case operators::op_less_less:     return {mf::bitwise_left_shift, name};
        case operators::op_greater_greater: return {mf::bitwise_right_shift, name};
#endif
        default: return {mf::index, nullptr};
    }
}

} // namespace welder::rods::sol2
