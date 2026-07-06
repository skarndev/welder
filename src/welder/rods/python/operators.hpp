#pragma once
#include <meta>

#include <welder/bind_traits.hpp> // is_unary_operator

/** @file
    The C++-operator → Python special-method ("dunder") map shared by welder's
    Python backends.

    pybind11 and nanobind expose the *same* Python object model, so a member
    operator maps to the same dunder in both (`operator+` → `__add__`, …). This
    header holds that one map so neither backend re-derives it — the Python analogue
    of `<welder/rods/lua/overloads.hpp>` and `<welder/rods/python/doc_style.hpp>`.
    A backend calls @ref welder::rods::python::operator_dunder from its
    `special_method_name` (the map that both gates operator eligibility and names the
    slot) and its `add_operator`.

    Requires the welder vocabulary to be available first (via `import welder;` or
    `#include <welder/vocabulary.hpp>`), like the rest of the reflection layer.
*/

namespace welder::rods::python {

/** The Python special-method ("dunder") name for a member operator, or `nullptr`
    if welder does not expose that operator.

    Unary vs binary is told apart by arity (a member operator takes 0 parameters
    when unary, 1 when binary), disambiguating the operators that have both forms
    (`+`, `-`). In-place compound assignments (`operator+=`, …) are intentionally
    not mapped: Python already falls back to the binary form
    (`a += b` → `a = a + b` via `__add__`) with correct value semantics, and
    binding `__iadd__` faithfully would need a reference return policy. Likewise
    `<=>`, `&&`, `||`, `++`, `--` and `=` have no clean reflection-driven Python
    mapping yet.

    @param f a reflection of the operator function.
    @return the dunder name (static storage), or `nullptr`.
*/
consteval const char* operator_dunder(std::meta::info f) {
    using std::meta::operators;
    const bool unary{::welder::detail::is_unary_operator(f)};
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:    return unary ? "__pos__" : "__add__";
        case operators::op_minus:   return unary ? "__neg__" : "__sub__";
        case operators::op_star:    return unary ? nullptr : "__mul__"; // unary * = deref
        case operators::op_slash:   return "__truediv__";
        case operators::op_percent: return "__mod__";
        case operators::op_tilde:   return "__invert__";
        case operators::op_caret:   return "__xor__";
        case operators::op_ampersand: return unary ? nullptr : "__and__"; // unary & = address-of
        case operators::op_pipe:    return "__or__";
        case operators::op_less_less:       return "__lshift__";
        case operators::op_greater_greater: return "__rshift__";
        case operators::op_equals_equals:      return "__eq__";
        case operators::op_exclamation_equals: return "__ne__";
        case operators::op_less:           return "__lt__";
        case operators::op_greater:        return "__gt__";
        case operators::op_less_equals:    return "__le__";
        case operators::op_greater_equals: return "__ge__";
        case operators::op_parentheses:     return "__call__";
        case operators::op_square_brackets: return "__getitem__";
        default:                            return nullptr;
    }
}

} // namespace welder::rods::python
