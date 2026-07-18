#pragma once
#include <meta>

#include <welder/bind_traits.hpp> // is_unary_operator

/** @file
    The C++-operator → Python special-method ("dunder") map shared by welder's
    Python backends.

    pybind11 and nanobind expose the *same* Python object model, so a member
    operator maps to the same dunder in both (`operator+` → `__add__`, …). This
    header holds that one map so neither backend re-derives it — the Python analogue
    of `<welder/rods/lua/metamethods.hpp>` and `<welder/rods/python/doc_style.hpp>`.
    A backend calls @ref welder::rods::python::operator_dunder from its
    `special_method_name` (the map that both gates operator eligibility and names the
    slot) and its `add_operator`.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`), like
    the rest of the reflection layer.
*/

namespace welder::inline v0::rods::python {

/** The Python special-method ("dunder") name for an operator (member or
    anchored free), or `nullptr` if welder does not expose that operator.

    Unary vs binary is told apart by arity (@ref welder::detail::is_unary_operator
    — a member operator's left operand is implicit, a free one spells both out),
    disambiguating the operators that have both forms (`+`, `-`). In-place
    compound assignments (`operator+=`, …) are intentionally not mapped: Python
    already falls back to the binary form (`a += b` → `a = a + b` via `__add__`)
    with correct value semantics, and binding `__iadd__` faithfully would need a
    reference return policy. Likewise `&&`, `||`, `++`, `--` and `=` have no
    clean reflection-driven Python mapping. `operator<=>` never binds under a
    dunder of its own — it synthesizes the relational slots instead (see the
    rods' `add_comparisons`).

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

/** The **reflected** ("swapped-operand") dunder for a free operator whose
    anchor type is the *right* operand — `operator*(double, Vec)` binds on `Vec`
    as `__rmul__`, so `2.0 * v` works from Python exactly as from C++. A
    comparison's reflected pair is its mirror image (Python has no `__rlt__`:
    `5 < v` falls through to `v.__gt__(5)`), and `==`/`!=` are their own mirrors.
    The rod binds such an entry through an operand-swapping wrapper (the C++
    operator still receives its operands in declaration order).

    @param f a reflection of the (binary, anchored-right) free operator.
    @return the reflected dunder name (static storage), or `nullptr`.
*/
consteval const char* reflected_dunder(std::meta::info f) {
    using std::meta::operators;
    if (::welder::detail::is_unary_operator(f))
        return nullptr; // a unary operator has no second operand to reflect
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:    return "__radd__";
        case operators::op_minus:   return "__rsub__";
        case operators::op_star:    return "__rmul__";
        case operators::op_slash:   return "__rtruediv__";
        case operators::op_percent: return "__rmod__";
        case operators::op_caret:   return "__rxor__";
        case operators::op_ampersand: return "__rand__";
        case operators::op_pipe:    return "__ror__";
        case operators::op_less_less:       return "__rlshift__";
        case operators::op_greater_greater: return "__rrshift__";
        // Comparisons mirror rather than prefix with r.
        case operators::op_equals_equals:      return "__eq__";
        case operators::op_exclamation_equals: return "__ne__";
        case operators::op_less:           return "__gt__";
        case operators::op_greater:        return "__lt__";
        case operators::op_less_equals:    return "__ge__";
        case operators::op_greater_equals: return "__le__";
        default:                           return nullptr;
    }
}

/** Whether @a f's slot participates in Python's `NotImplemented` protocol —
    every binary arithmetic / bitwise / shift / comparison dunder (reflected
    forms included), where a failed operand conversion must return
    `NotImplemented` so Python tries the other operand's reflected method,
    rather than raise `TypeError`. `__call__` and `__getitem__` are excluded
    (there `TypeError` IS the protocol), as are the unary slots. The Python rods
    pass `is_operator()` on exactly these defs.

    @param f a reflection of the operator function.
    @return `true` iff the def should carry the backend's `is_operator` tag.
*/
consteval bool dunder_uses_not_implemented(std::meta::info f) {
    using std::meta::operators;
    if (::welder::detail::is_unary_operator(f))
        return false;
    switch (std::meta::operator_of(f)) {
        case operators::op_parentheses:
        case operators::op_square_brackets: return false;
        default: return operator_dunder(f) != nullptr;
    }
}

/** The dunder each synthesized comparison slot binds under (see the rods'
    `add_comparisons` and @ref welder::detail::synthesized_comparison).
    @param s the relational slot.
    @return the dunder name (static storage). */
consteval const char* cmp_slot_dunder(::welder::detail::cmp_slot s) {
    switch (s) {
        case ::welder::detail::cmp_slot::lt: return "__lt__";
        case ::welder::detail::cmp_slot::le: return "__le__";
        case ::welder::detail::cmp_slot::gt: return "__gt__";
        case ::welder::detail::cmp_slot::ge: return "__ge__";
    }
    return nullptr;
}

/** The comparison-synthesis walk shared by both Python backends: for each
    `operator<=>` overload in @a Fns, hand @a def a
    (@ref welder::detail::synthesized_comparison function pointer, dunder name)
    pair per relational slot not already @a Covered by an explicit operator.
    The wrapper is a plain rewritten expression over (@a T, the overload's
    operand type), so C++'s own rewriting semantics carry over verbatim.
    @tparam T       the welded type.
    @tparam Fns     the spaceship overload group (a static array of reflections).
    @tparam Covered the explicitly-covered slot flags, indexed by
                    @ref welder::detail::cmp_slot.
    @param def a callable receiving `(const char* dunder, bool (*)(…))`. */
template <class T, auto Fns, auto Covered, class Def>
void synthesize_comparisons(Def def) {
    using ::welder::detail::cmp_slot;
    using ::welder::detail::synthesized_comparison;
    template for (constexpr auto fn : std::define_static_array(Fns)) {
        using P = std::remove_cvref_t<
            typename [: ::welder::detail::comparison_operand(fn, ^^T) :]>;
        if constexpr (!Covered[0])
            def(cmp_slot_dunder(cmp_slot::lt),
                &synthesized_comparison<T, P, cmp_slot::lt>::call);
        if constexpr (!Covered[1])
            def(cmp_slot_dunder(cmp_slot::le),
                &synthesized_comparison<T, P, cmp_slot::le>::call);
        if constexpr (!Covered[2])
            def(cmp_slot_dunder(cmp_slot::gt),
                &synthesized_comparison<T, P, cmp_slot::gt>::call);
        if constexpr (!Covered[3])
            def(cmp_slot_dunder(cmp_slot::ge),
                &synthesized_comparison<T, P, cmp_slot::ge>::call);
    }
}

} // namespace welder::rods::python
