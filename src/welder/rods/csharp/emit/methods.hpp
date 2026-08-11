#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>

#include <welder/rods/csharp/directors.hpp>
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/callables.hpp>
#include <welder/rods/csharp/emit/containers.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/operators.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **Methods**: an overload group becomes natural C# overloads sharing one name,
    each with its own indexed symbol, so the shim re-derives the exact overload
    rather than resolving one over wire types.

    An **overridable virtual slot** of a director-eligible type is the one shape
    that needs more than a thunk: it emits as `public virtual` and dispatches by
    origin. A director-constructed instance takes a *qualified base-call* thunk —
    C# has already resolved the dynamic dispatch, and this is also what makes
    `base.Method()` inside an override terminate — while a C++-originated object
    takes the ordinary virtual thunk, since its dynamic type may be a C++
    subclass the managed side has never heard of.
*/

namespace welder::inline v0::rods::csharp {

/** Emit virtual slot @a Fn: the ordinary (virtual-dispatch) thunk, a
    qualified base-call thunk, and a `public virtual` wrapper branching on
    `_isDirector`. Records the slot's C# name for the director
    scaffolding's placeholders. */
template <std::meta::info Fn, class Style>
void emit_virtual_method(class_writer& w, const std::string& name,
                                 const std::string& sym,
                                 const std::string& expr,
                                 std::size_t slot) {
    {
        const std::string ks{std::to_string(slot)};

        ::welder::validate_return_policy<Fn, lang::cs>();
        constexpr bool ret_checked{
            (require_marshallable(std::meta::return_type_of(Fn), true),
             true)};
        static_assert(ret_checked);
        constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
        const call_pieces cp{
            build_params<Fn, Style>(std::make_index_sequence<n>{})};
        const std::string bsym{sym + "_base"};
        w.doc->record_symbol(sym);
        w.doc->record_symbol(bsym);

        const std::string idx{"wcs::director_slot(^^" + w.cpp_qualified +
                              ", " + ks + ")"};
        std::string shim_params{"void* self" +
                                (cp.shim_params.empty()
                                     ? std::string{}
                                     : ", " + cp.shim_params) +
                                ", welder_error* err"};
        std::string delegate_args{"self, err" +
                                  (cp.delegate_args.empty()
                                       ? std::string{}
                                       : ", " + cp.delegate_args)};
        // 1) the ordinary thunk (virtual dispatch through the pmf)
        w.doc->shim += std::string{
                           wire_return_v<std::meta::return_type_of(Fn)>} +
                       " " + sym + "(" +
                       shim_params + ") { return " + expr + "(" +
                       delegate_args + "); }\n\n";
        // 2) the qualified base-call thunk (never re-enters the director)
        {
            std::string conv{};
            [[maybe_unused]] std::size_t j{0};
            template for ([[maybe_unused]] constexpr auto p :
                          std::define_static_array(
                              std::meta::parameters_of(Fn))) {
                const std::string js{std::to_string(j)};
                conv += (j ? ", " : "");
                conv += "wcs::shim::to_cpp<::std::meta::type_of(::std::"
                        "meta::parameters_of(" +
                        idx + ")[" + js + "])>(a" + js + ")";
                ++j;
            }
            static constexpr const char* fid{std::define_static_string(
                std::meta::identifier_of(Fn))};
            w.doc->shim +=
                std::string{
                    wire_return_v<std::meta::return_type_of(Fn)>} +
                " " + bsym + "(" +
                shim_params + ") {\n    auto* _o = static_cast<" +
                w.cpp_qualified +
                "*>(self);\n    return wcs::shim::guarded<"
                "::std::meta::return_type_of(" +
                idx + ")>(err, [&]() -> decltype(auto) { return _o->" +
                w.cpp_qualified + "::" + fid + "(" + conv +
                "); });\n}\n\n";
        }
        // P/Invokes for both
        std::string pin_params{w.handle_cs + " self" +
                               (cp.pinvoke_params.empty()
                                    ? std::string{}
                                    : ", " + cp.pinvoke_params) +
                               ", out WelderError err"};
        constexpr bool r_is_bool{
            classify(std::meta::return_type_of(Fn)) ==
            marshal_kind::boolean};
        const std::string ret_attr{
            r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : ""};
        for (const std::string& s2 : {sym, bsym})
            w.doc->pinvoke += "        " + import_attr(cp.has_string) + " " +
                              ret_attr + "internal static partial " +
                              pinvoke_type<std::meta::return_type_of(Fn),
                                           Style>(true) +
                              " " + s2 + "(" + pin_params + ");\n";
        // 3) the public virtual wrapper, branching by origin
        emit_callable_docs<Fn>(w.members, "        ", cp);
        std::string call_args{w.handle_field +
                              (cp.wrapper_args.empty()
                                   ? std::string{}
                                   : ", " + cp.wrapper_args) +
                              ", out WelderError _e"};
        w.members += "        public virtual " +
                     public_return_type<std::meta::return_type_of(Fn),
                                        Style>() +
                     " " + name + "(" + cp.wrapper_params +
                     ")\n        {\n            if (_isDirector)\n"
                     "            {\n";
        w.members += wrapper_return_body<std::meta::return_type_of(Fn),
                                         Style,
                                         ::welder::return_policy_of(
                                             Fn, lang::cs)>(
            "NativeMethods." + bsym + "(" + call_args + ")",
            "                ", "this");
        w.members += "            }\n            else\n            {\n";
        w.members += wrapper_return_body<std::meta::return_type_of(Fn),
                                         Style,
                                         ::welder::return_policy_of(
                                             Fn, lang::cs)>(
            "NativeMethods." + sym + "(" + call_args + ")",
            "                ", "this");
        w.members += "            }\n        }\n\n";
    }
}
/** Emit method overload group @a Fns as natural C# overloads sharing one
    name, each with its own indexed symbol (the group's name resolves from
    `Fns[0]`; the index re-derives the exact overload shim-side). An
    overridable virtual slot of a director-eligible type emits as a
    `public virtual` method that dispatches by origin — the ordinary
    virtual thunk for a C++-originated object, the qualified base-call
    thunk for a director (C# has already resolved the dynamic dispatch;
    this is also what terminates `base.Method()` inside an override). */
template <auto Fns, class Style = ::welder::naming::none>
void emit_method_group(class_writer& w) {
    const std::string name{::welder::name_of<Fns[0], lang::cs, Style,
                                             ::welder::ent_kind::method>()};
    w.surface_names.push_back(name);
    const std::string anchor{w.cpp_anchor};
    template for (constexpr auto fn : std::define_static_array(Fns)) {
        constexpr std::size_t k{index_of_named_member(fn)};
        const std::string id{std::meta::identifier_of(fn)};
        const std::string sym{w.sym_prefix + "_m_" + id + "_" +
                              std::to_string(k)};
        constexpr bool named_parent{
            spellable(std::meta::parent_of(fn))};
        const std::string fowner{owner_expr(
            named_parent, cpp_name_v<std::meta::parent_of(fn)>,
            w.cpp_qualified, anchor)};
        const std::string expr{"wcs::shim::method<" + anchor +
                               ", wcs::named_member(" + fowner + ", \"" +
                               id + "\", " + std::to_string(k) + ")>"};
        static constexpr const char* fsig{std::define_static_string(
            std::meta::display_string_of(std::meta::type_of(fn)))};
        // The director scaffolding names slots through render-time
        // placeholders keyed by (declaring class, identifier, signature) —
        // record every bound method, so an inherited slot resolves through
        // the BASE wrapper's binding.
        w.doc->record_type_name(
            std::string{cpp_name_v<std::meta::parent_of(fn)>} + "#" + id +
                "#" + fsig,
            name);
        collect_containers<fn>(*w.doc);
        std::size_t vslot_k{static_cast<std::size_t>(-1)};
        if constexpr (std::meta::is_virtual(fn)) {
            if (w.is_director) {
                static constexpr const char* vid{std::define_static_string(
                    std::meta::identifier_of(fn))};
                for (const auto& vs : w.vslots)
                    if (std::string_view{vs.name} == vid &&
                        std::string_view{vs.sig} == fsig)
                        vslot_k = vs.k;
            }
        }
        if (vslot_k != static_cast<std::size_t>(-1)) {
            emit_virtual_method<fn, Style>(w, name, sym, expr, vslot_k);
        } else {
            emit_callable<fn, Style, true>(*w.doc, sym, w.members,
                                           "        ", name, expr,
                                           w.handle_cs,
                                           w.handle_field);
        }
    }
}

/** Emit static-method overload group @a Fns as `public static` overloads. */
template <auto Fns, class Style = ::welder::naming::none>
void emit_static_method_group(class_writer& w) {
    const std::string name{
        ::welder::name_of<Fns[0], lang::cs, Style,
                          ::welder::ent_kind::static_method>()};
    w.surface_names.push_back(name);
    template for (constexpr auto fn : std::define_static_array(Fns)) {
        constexpr std::size_t k{index_of_named_member(fn)};
        collect_containers<fn>(*w.doc);
        const std::string id{std::meta::identifier_of(fn)};
        const std::string sym{w.sym_prefix + "_s_" + id + "_" +
                              std::to_string(k)};
        constexpr bool named_parent{
            spellable(std::meta::parent_of(fn))};
        const std::string expr{
            "wcs::shim::function<wcs::named_member(" +
            owner_expr(named_parent,
                        cpp_name_v<std::meta::parent_of(fn)>,
                        w.cpp_qualified, w.cpp_anchor) +
            ", \"" + id + "\", " + std::to_string(k) + ")>"};
        emit_callable<fn, Style, false>(*w.doc, sym, w.members, "        ",
                                        name, expr);
    }
}
/** Bind the swept free ostream inserter @a Fn as `ToString()` (via
    @ref welder::detail::stringify, dup'd across the wire). */
template <class T, std::meta::info Fn>
void emit_stringifier(class_writer& w) {
    const std::string sym{w.sym_prefix + "_str"};
    w.doc->record_symbol(sym);
    constexpr std::size_t k{index_of_operator(Fn)};
    static constexpr const char* opid{std::define_static_string(
        operator_enum_ident(std::meta::operator_of(Fn)))};
    w.doc->shim += "const char* " + sym +
                   "(void* self, welder_error* err) { return "
                   "wcs::shim::stringify_text<" + w.cpp_anchor +
                   ", wcs::named_operator(^^" +
                   std::string{cpp_name_v<std::meta::parent_of(Fn)>} +
                   ", std::meta::operators::" + opid + ", false, " +
                   std::to_string(k) + ")>(self, err); }\n\n";
    w.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial "
                      "IntPtr " + sym + "(" + w.handle_cs +
                      " self, out WelderError err);\n";
    w.members += "        public override string ToString()\n        {\n";
    w.members += wrapper_return_body<^^std::string, ::welder::naming::none>(
        "NativeMethods." + sym + "(" + w.handle_field +
            ", out WelderError _e)",
        "            ");
    w.members += "        }\n\n";
}
} // namespace welder::inline v0::rods::csharp
