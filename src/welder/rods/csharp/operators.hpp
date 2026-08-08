#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>

#include <welder/bind_traits.hpp>          // is_unary_operator / operator helpers
#include <welder/rods/csharp/type_map.hpp> // the shared lookup-layer conventions

/** @file
    The C++-operator → **C#** map of the C# rod, and the operator half of the
    shared member-lookup layer (operators have no identifier, so `named_member`
    cannot re-derive them — @ref named_operator looks up by (operator token,
    arity, declaration index) instead, with the same generator ⇄ shim
    agreement contract).

    C# operator overloading is close to C++'s but not identical, so each C++
    operator resolves to one of five shapes (@ref cs_op_kind):
    - **binary / unary** → a `public static` C# operator (`op_Addition`, …);
    - **comparison** → a static operator too, but routed through the class
      writer's pairing ledger: C# requires `==`/`!=`, `<`/`>` and `<=`/`>=`
      in pairs, so a partner C++ never declared is synthesized (negation for
      equality, operand-swap for homogeneous relationals) — and a heterogeneous
      relational whose partner cannot be synthesized demotes to a named method;
    - **indexer** (`operator[]`) → `this[…]` (get-only);
    - **invoke** (`operator()`) → a plain `Invoke(…)` method.
    Assignment, compound assignment, `++`/`--`, dereference and the exotic
    operators do not map (`kind == none` → `special_method_name` returns
    `nullptr`, gating them off in the carriage).
*/

namespace welder::inline v0::rods::csharp {

/** The C# shape a C++ operator maps to. */
enum class cs_op_kind {
    none,       /**< Not representable — gated off. */
    binary,     /**< A `public static` binary operator. */
    unary,      /**< A `public static` unary operator. */
    comparison, /**< A comparison operator (pairing-ledger routed). */
    indexer,    /**< `operator[]` → `this[…]`. */
    invoke,     /**< `operator()` → `Invoke(…)`. */
};

/** One operator's mapping row. */
struct cs_op_info {
    cs_op_kind kind{cs_op_kind::none};
    const char* symbol{nullptr};   /**< The C# operator token (`"+"`, `"=="`). */
    const char* cls_name{nullptr}; /**< The CLS metadata name (`op_Addition`). */
};

/** The mapping row for operator function @a fn (member or free; unary-ness
    per @ref welder::detail::is_unary_operator). */
consteval cs_op_info cs_operator(std::meta::info fn) {
    namespace m = std::meta;
    const bool u{::welder::detail::is_unary_operator(fn)};
    switch (m::operator_of(fn)) {
        using enum m::operators;
        case op_plus:
            return u ? cs_op_info{cs_op_kind::unary, "+", "op_UnaryPlus"}
                     : cs_op_info{cs_op_kind::binary, "+", "op_Addition"};
        case op_minus:
            return u ? cs_op_info{cs_op_kind::unary, "-", "op_UnaryNegation"}
                     : cs_op_info{cs_op_kind::binary, "-", "op_Subtraction"};
        case op_star:
            return u ? cs_op_info{} // dereference does not map
                     : cs_op_info{cs_op_kind::binary, "*", "op_Multiply"};
        case op_slash:
            return {cs_op_kind::binary, "/", "op_Division"};
        case op_percent:
            return {cs_op_kind::binary, "%", "op_Modulus"};
        case op_caret:
            return {cs_op_kind::binary, "^", "op_ExclusiveOr"};
        case op_ampersand:
            return u ? cs_op_info{} // address-of does not map
                     : cs_op_info{cs_op_kind::binary, "&", "op_BitwiseAnd"};
        case op_pipe:
            return {cs_op_kind::binary, "|", "op_BitwiseOr"};
        case op_less_less:
            return {cs_op_kind::binary, "<<", "op_LeftShift"};
        case op_greater_greater:
            return {cs_op_kind::binary, ">>", "op_RightShift"};
        case op_tilde:
            return {cs_op_kind::unary, "~", "op_OnesComplement"};
        case op_exclamation:
            return {cs_op_kind::unary, "!", "op_LogicalNot"};
        case op_equals_equals:
            return {cs_op_kind::comparison, "==", "op_Equality"};
        case op_exclamation_equals:
            return {cs_op_kind::comparison, "!=", "op_Inequality"};
        case op_less:
            return {cs_op_kind::comparison, "<", "op_LessThan"};
        case op_greater:
            return {cs_op_kind::comparison, ">", "op_GreaterThan"};
        case op_less_equals:
            return {cs_op_kind::comparison, "<=", "op_LessThanOrEqual"};
        case op_greater_equals:
            return {cs_op_kind::comparison, ">=", "op_GreaterThanOrEqual"};
        case op_square_brackets:
            return {cs_op_kind::indexer, "[]", "op_Index"};
        case op_parentheses:
            return {cs_op_kind::invoke, "()", "Invoke"};
        default:
            return {};
    }
}

/** The enumerator identifier of @a op (`"op_plus"`, …) — for spelling the
    lookup in the generated shim (`std::meta::operators::op_plus`). */
consteval std::string operator_enum_ident(std::meta::operators op) {
    for (std::meta::info e : std::meta::enumerators_of(^^std::meta::operators))
        if (std::meta::extract<std::meta::operators>(e) == op)
            return std::string{std::meta::identifier_of(e)};
    throw diag::csharp_member_lookup_mismatch{};
}

/** The @a K-th operator-function member of @a owner with token @a op and the
    given @a unary-ness, in declaration order — @ref named_member's operator
    twin (operators have no identifier). Same participation-independent
    indexing, same loud failure on drift. */
consteval std::meta::info named_operator(std::meta::info owner,
                                         std::meta::operators op, bool unary,
                                         std::size_t k) {
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_operator_function(m) &&
            std::meta::operator_of(m) == op &&
            ::welder::detail::is_unary_operator(m) == unary) {
            if (seen == k)
                return m;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** @ref named_operator's inverse: the declaration-order index of operator
    function @a fn among its scope's same-(token, arity) operators. */
consteval std::size_t index_of_operator(std::meta::info fn) {
    const std::meta::info owner{std::meta::parent_of(fn)};
    const std::meta::operators op{std::meta::operator_of(fn)};
    const bool unary{::welder::detail::is_unary_operator(fn)};
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_operator_function(m) &&
            std::meta::operator_of(m) == op &&
            ::welder::detail::is_unary_operator(m) == unary) {
            if (m == fn)
                return seen;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

} // namespace welder::inline v0::rods::csharp
