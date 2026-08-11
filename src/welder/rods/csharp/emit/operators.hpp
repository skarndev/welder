#pragma once
#include <algorithm>
#include <cstddef>
#include <meta>
#include <string>
#include <vector>

#include <welder/bind_traits.hpp>                  // is_unary_operator / comparison_operand
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/operators.hpp>        // the C++-operator -> C# map
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **Operators, by shape.** The C++ → C# operator map lives in
    `<welder/rods/csharp/operators.hpp>`; this is the emitter that acts on it.

    Three of the five shapes emit directly — a static `operator`, a get-only
    indexer, an `Invoke` method — while **comparisons** cannot: C# requires
    `==`/`!=`, `<`/`>` and `<=`/`>=` in pairs, which is only decidable once the
    whole class surface is known. Those are recorded in the class writer's
    pairing ledger and rendered at flush (see
    `<welder/rods/csharp/document/class_writer.hpp>`).

    @ref welder::rods::csharp::emit_comparison_set is the `operator<=>` path: one
    compare thunk collapses the ordering to an int, and the four relational
    operators are synthesized from it — minus any slot an explicit participating
    operator already covers, plus the reversed operand order for a heterogeneous
    overload (C++'s rewritten `5 < obj` works, so C#'s should).
*/

namespace welder::inline v0::rods::csharp {

/** The `wcs::named_operator(...)` lookup text for operator @a Fn.
    @param anchor the bound type's `^^…` anchor — the declaring-scope
    fallback when @a Fn's parent is an unspellable specialization. */
template <std::meta::info Fn>
std::string operator_lookup(const std::string& anchor) {
    static constexpr const char* opid{std::define_static_string(
        operator_enum_ident(std::meta::operator_of(Fn)))};
    constexpr bool u{::welder::detail::is_unary_operator(Fn)};
    constexpr std::size_t k{index_of_operator(Fn)};
    constexpr bool named_parent{
        spellable(std::meta::parent_of(Fn))};
    return "wcs::named_operator(" +
           (named_parent
                ? "^^" + std::string{cpp_name_v<std::meta::parent_of(Fn)>}
                : anchor) +
           ", std::meta::operators::" + opid + (u ? ", true, " : ", false, ") +
           std::to_string(k) + ")";
}

/** The `_op_<token>_<u|b>_<k>` symbol suffix for operator @a Fn. */
template <std::meta::info Fn>
std::string operator_sym(const class_writer& w) {
    static constexpr const char* opid{std::define_static_string(
        operator_enum_ident(std::meta::operator_of(Fn)))};
    constexpr bool u{::welder::detail::is_unary_operator(Fn)};
    constexpr std::size_t k{index_of_operator(Fn)};
    // strip the "op_" prefix for the symbol leaf
    return w.sym_prefix + "_op_" + (opid + 3) + (u ? "_u_" : "_b_") +
           std::to_string(k);
}

/** Emit one operator overload @a Fn of welded type @a T. */
template <class T, std::meta::info Fn>
void emit_operator(class_writer& w) {
    constexpr cs_op_info oi{cs_operator(Fn)};
    static_assert(oi.kind != cs_op_kind::none,
                  "welder: unmapped operator reached add_operator");
    ::welder::validate_return_policy<Fn, lang::cs>();
    constexpr bool is_member{std::meta::is_class_member(Fn)};
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    const call_pieces cp{
        build_params<Fn, ::welder::naming::none>(std::make_index_sequence<n>{})};
    const std::string sym{operator_sym<Fn>(w)};
    w.doc->record_symbol(sym);

    // --- shim thunk + P/Invoke (uniform: member -> method, free -> function)
    std::string shim_params{cp.shim_params};
    std::string delegate_args{};
    std::string pin_params{cp.pinvoke_params};
    if constexpr (is_member) {
        shim_params = "void* self" +
                      (cp.shim_params.empty() ? "" : ", " + cp.shim_params);
        delegate_args = "self, err";
        pin_params = w.handle_cs + " self" +
                     (cp.pinvoke_params.empty() ? "" : ", " + cp.pinvoke_params);
    } else {
        delegate_args = "err";
    }
    if (!cp.delegate_args.empty())
        delegate_args += ", " + cp.delegate_args;
    shim_params += (shim_params.empty() ? "" : ", ");
    shim_params += "welder_error* err";
    pin_params += (pin_params.empty() ? "" : ", ");
    pin_params += "out WelderError err";
    const std::string expr{
        is_member ? "wcs::shim::method<" + w.cpp_anchor + ", " +
                        operator_lookup<Fn>(w.cpp_anchor) + ">"
                  : "wcs::shim::function<" + operator_lookup<Fn>(w.cpp_anchor) + ">"};
    w.doc->shim += std::string{
                       wire_return_v<std::meta::return_type_of(Fn)>} +
                   " " + sym + "(" + shim_params + ") { return " + expr +
                   "(" + delegate_args + "); }\n\n";
    constexpr bool r_is_bool{classify(std::meta::return_type_of(Fn)) ==
                             marshal_kind::boolean};
    w.doc->pinvoke +=
        "        " + import_attr(cp.has_string) + " " +
        (r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "") +
        "internal static partial " +
        pinvoke_type<std::meta::return_type_of(Fn),
                     ::welder::naming::none>(true) +
        " " + sym + "(" + pin_params + ");\n";

    // --- managed surface, by shape --------------------------------------
    // Operand spellings: `this`-typed for a member's left, declared
    // otherwise; the wrapper call rebuilds the argument expressions with
    // the operator parameter names (member left = `l`, others per shape).
    if constexpr (oi.kind == cs_op_kind::invoke) {
        // operator() -> a plain Invoke method (self + params).
        std::string args{w.handle_field +
                         (cp.wrapper_args.empty() ? "" : ", " + cp.wrapper_args)};
        w.members += "        public " +
                     public_return_type<std::meta::return_type_of(Fn),
                                        ::welder::naming::none>() +
                     " Invoke(" + cp.wrapper_params + ")\n        {\n";
        w.members += wrapper_return_body<std::meta::return_type_of(Fn),
                                         ::welder::naming::none,
                                         ::welder::return_policy_of(
                                             Fn, lang::cs)>(
            "NativeMethods." + sym + "(" + args + ", out WelderError _e)",
            "            ", "this");
        w.members += "        }\n\n";
    } else if constexpr (oi.kind == cs_op_kind::indexer) {
        if (std::find(w.indexer_sigs.begin(), w.indexer_sigs.end(),
                      cp.wrapper_params) != w.indexer_sigs.end())
            return; // const/non-const C++ pair -> one C# indexer
        w.indexer_sigs.push_back(cp.wrapper_params);
        w.members += "        public " +
                     public_return_type<std::meta::return_type_of(Fn),
                                        ::welder::naming::none>() +
                     " this[" + cp.wrapper_params +
                     "]\n        {\n            get\n            {\n";
        w.members += wrapper_return_body<std::meta::return_type_of(Fn),
                                         ::welder::naming::none,
                                         ::welder::return_policy_of(
                                             Fn, lang::cs)>(
            "NativeMethods." + sym + "(" + w.handle_field + ", " +
                cp.wrapper_args + ", out WelderError _e)",
            "                ", "this");
        w.members += "            }\n        }\n\n";
    } else {
        // A static operator. Operand list: member -> (T l, P r) / (T v);
        // free -> the declared order (reflected entries included — C#
        // allows any order as long as one operand is the class).
        std::vector<std::string> op_types{};
        std::vector<std::string> op_names{};
        std::string args{}; // the P/Invoke argument expressions
        if constexpr (is_member) {
            op_types.push_back(type_ref<^^T>());
            op_names.push_back("l");
            args = "l._h_" + field_ref<^^T>();
        }
        // Guard the empty pack: param_types<Fn> must not instantiate for
        // a parameterless callable (a unary member operator) — the
        // array<info, 0> gotcha (see build_params).
        if constexpr (n != 0) {
            static constexpr auto pts{::welder::detail::param_types<Fn>()};
            std::size_t j{0};
            template for (constexpr auto pt : std::define_static_array(pts)) {
                const std::string nm{is_member ? "r" : (j == 0 ? "l" : "r")};
                op_types.push_back(public_type<pt, ::welder::naming::none>());
                op_names.push_back(nm);
                args += (args.empty() ? "" : ", ");
                if constexpr (classify(pt) == marshal_kind::handle)
                    args += nm + "._h_" + field_ref<bare(pt)>();
                else
                    args += nm;
                ++j;
            }
        }
        std::string params{};
        for (std::size_t i{0}; i < op_types.size(); ++i) {
            params += (params.empty() ? "" : ", ");
            params += op_types[i] + " " + op_names[i];
        }
        std::string body{wrapper_return_body<
            std::meta::return_type_of(Fn), ::welder::naming::none,
            ::welder::return_policy_of(Fn, lang::cs)>(
            "NativeMethods." + sym + "(" + args + ", out WelderError _e)",
            "            ")};
        if constexpr (oi.kind == cs_op_kind::comparison) {
            // Binary always; the pairing ledger decides operator-vs-named
            // at flush.
            class_writer::cs_comparison c{};
            c.op = oi.symbol;
            c.lhs = op_types[0];
            c.rhs = op_types[1];
            c.ret = public_return_type<std::meta::return_type_of(Fn),
                                       ::welder::naming::none>();
            c.body = std::move(body);
            if (!w.have_comparison(c.op, c.lhs, c.rhs))
                w.comparisons.push_back(std::move(c));
        } else if constexpr (oi.kind == cs_op_kind::unary) {
            w.members += "        public static " +
                         public_return_type<std::meta::return_type_of(Fn),
                                            ::welder::naming::none>() +
                         " operator " + oi.symbol + "(" + op_types[0] +
                         " v)\n        {\n";
            // The single operand is named `l`/`v` per shape; rebuild the
            // call with `v`.
            w.members += wrapper_return_body<
                std::meta::return_type_of(Fn), ::welder::naming::none,
                ::welder::return_policy_of(Fn, lang::cs)>(
                "NativeMethods." + sym + "(" +
                    (is_member ? "v._h_" + field_ref<^^T>() : std::string{"v"}) +
                    ", out WelderError _e)",
                "            ");
            w.members += "        }\n\n";
        } else {
            w.members += "        public static " +
                         public_return_type<std::meta::return_type_of(Fn),
                                            ::welder::naming::none>() +
                         " operator " + oi.symbol + "(" + params +
                         ")\n        {\n" + body + "        }\n\n";
        }
    }
}

/** Emit the compare thunk + the (un-@a Covered) relational operators for
    one spaceship overload @a Fn of @a T. */
template <class T, std::meta::info Fn, auto Covered>
void emit_comparison_set(class_writer& w) {
    constexpr std::size_t k{index_of_operator(Fn)};
    const std::string sym{w.sym_prefix + "_cmp_" + std::to_string(k)};
    w.doc->record_symbol(sym);
    const std::string lookup{operator_lookup<Fn>(w.cpp_anchor)};
    // The operand: spelled through the SAME consteval re-derivation the
    // generator used, so the shim's compare<> sees the identical type.
    const std::string opnd{"::welder::detail::comparison_operand(" + lookup +
                           ", " + w.cpp_anchor + ")"};
    w.doc->shim +=
        "std::int32_t " + sym + "(void* self, " +
        std::string{wire_param_v<::welder::detail::comparison_operand(
            Fn, ^^T)>} +
        " a0, welder_error* err) { return wcs::shim::compare<" +
        w.cpp_anchor + ", " + opnd + ">(self, err, a0); }\n\n";
    constexpr bool p_is_bool{
        classify(::welder::detail::comparison_operand(Fn, ^^T)) ==
        marshal_kind::boolean};
    w.doc->pinvoke +=
        "        [LibraryImport(Lib)] internal static partial int " + sym +
        "(" + w.handle_cs + " self, " +
        (p_is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
        pinvoke_type<::welder::detail::comparison_operand(Fn, ^^T),
                     ::welder::naming::none>(false) +
        " a0, out WelderError err);\n";

    const std::string lhs{type_ref<^^T>()};
    const std::string rhs{public_type<::welder::detail::comparison_operand(
                                          Fn, ^^T),
                                      ::welder::naming::none>()};
    constexpr bool rhs_is_handle{
        classify(::welder::detail::comparison_operand(Fn, ^^T)) ==
        marshal_kind::handle};
    // Computed under if constexpr: field_ref must not instantiate for a
    // scalar operand (no parent to spell).
    std::string rhs_field{};
    if constexpr (rhs_is_handle)
        rhs_field = "._h_" + field_ref<bare(::welder::detail::
                                                comparison_operand(
                                                    Fn, ^^T))>();
    const bool hetero{lhs != rhs};
    auto record = [&](const char* op, bool reversed, const char* cond) {
        class_writer::cs_comparison c{};
        c.op = op;
        c.lhs = reversed ? rhs : lhs;
        c.rhs = reversed ? lhs : rhs;
        c.ret = "bool";
        const std::string self_arg{(reversed ? "r._h_" : "l._h_") +
                                   field_ref<^^T>()};
        std::string other{reversed ? "l" : "r"};
        other += rhs_field;
        c.body = "            var _c = NativeMethods." + sym + "(" +
                 self_arg + ", " + other +
                 ", out WelderError _e);\n"
                 "            WelderInterop.ThrowIfError(in _e);\n"
                 "            return " + cond + ";\n";
        if (!w.have_comparison(c.op, c.lhs, c.rhs))
            w.comparisons.push_back(std::move(c));
    };
    if constexpr (!Covered[0]) record("<", false, "_c == -1");
    if constexpr (!Covered[1]) record("<=", false, "_c == -1 || _c == 0");
    if constexpr (!Covered[2]) record(">", false, "_c == 1");
    if constexpr (!Covered[3]) record(">=", false, "_c == 0 || _c == 1");
    if (hetero) {
        // The reversed operand order (C++'s rewritten `5 < obj`): the
        // relation flips around the same thunk.
        if constexpr (!Covered[2]) record("<", true, "_c == 1");
        if constexpr (!Covered[3]) record("<=", true, "_c == 0 || _c == 1");
        if constexpr (!Covered[0]) record(">", true, "_c == -1");
        if constexpr (!Covered[1]) record(">=", true, "_c == -1 || _c == 0");
    }
}
} // namespace welder::inline v0::rods::csharp
