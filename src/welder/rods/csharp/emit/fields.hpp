#pragma once
#include <cstddef>
#include <meta>
#include <string>

#include <welder/doc.hpp>
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/containers.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/text.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **Properties**: both kinds. A C# property can come from a data member
    (@ref welder::rods::csharp::emit_field) or from an accessor pair the driver
    resolved (@ref welder::rods::csharp::emit_property); both emit a getter
    thunk, an optional setter thunk, and the managed property calling them.

    The interesting decision is what a **field's read hands out**. A non-const
    class-typed or container-typed member yields a LIVE view tied to its parent
    — the runtime rods' `def_readwrite` reference_internal semantics — so that
    `obj.Items[0].Field = x` writes through. A non-const scalar/enum sequence
    member goes further, to a wrapper with a zero-copy `Span<T>` over the C++
    buffer (@ref welder::rods::csharp::emit_scalar_seq_field), rather than the
    by-value `T[]` snapshot params and returns use — a snapshot would make
    `obj.Nums.Add(…)` silently mutate a temporary. A const member, and every
    leaf kind, crosses by value.
*/

namespace welder::inline v0::rods::csharp {

template <std::meta::info Mem, class Style = ::welder::naming::none>
void emit_field(class_writer& w) {
    constexpr std::meta::info MT{std::meta::type_of(Mem)};
    // A non-const SCALAR/ENUM sequence member is a LIVE object, so it
    // binds by reference like its welded-element siblings — a generated
    // wrapper with live element access and a zero-copy AsSpan() — never a
    // by-value T[] snapshot (which would make `obj.Nums.Add(…)` silently
    // mutate a temporary). Params/returns keep the ergonomic T[] copies;
    // a const member stays a copy too (writing through its span would be
    // UB).
    if constexpr (classify(MT) == marshal_kind::seq_value &&
                  !std::meta::is_const_type(MT)) {
        emit_scalar_seq_field<Mem, Style>(w);
        return;
    }
    ensure_for<std::meta::type_of(Mem)>(*w.doc);
    constexpr bool checked{(require_marshallable(MT, true), true)};
    static_assert(checked);
    const std::string id{std::meta::identifier_of(Mem)};
    const std::string anchor{w.cpp_anchor};
    // The lookup is by the DECLARING scope (a flattened non-welded base's
    // member is not among the welded type's own members), and a flattened
    // member's symbol is namespaced by that scope — a base field shadowed by a
    // derived one of the same name would otherwise emit two identical symbols.
    const member_scope ms{member_scope_of<Mem>(w.cpp_qualified, anchor,
                                               w.type_token)};
    const std::string lookup{"wcs::named_field(" + ms.owner + ", \"" + id +
                             "\")"};
    const std::string getsym{w.sym_prefix + "_get_" + id + ms.suffix};
    const std::string setsym{w.sym_prefix + "_set_" + id + ms.suffix};
    constexpr bool read_only{std::meta::is_const_type(MT) ||
                             ::welder::member_no_reassign(Mem, lang::cs)};
    constexpr bool is_str{classify(MT) == marshal_kind::utf8_string};
    constexpr bool is_bool{classify(MT) == marshal_kind::boolean};

    // getter thunk + P/Invoke
    w.doc->record_symbol(getsym);
    w.doc->shim += std::string{wire_return_v<std::meta::type_of(Mem)>} +
                   " " + getsym +
                   "(void* self, welder_error* err) { return "
                   "wcs::shim::field_get<" + anchor + ", " + lookup +
                   ">(self, err); }\n\n";
    w.doc->pinvoke += "        " + import_attr(false) + " " +
                      (is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "") +
                      "internal static partial " +
                      pinvoke_type<std::meta::type_of(Mem), Style>(true) +
                      " " + getsym + "(" + w.handle_cs +
                      " self, out WelderError err);\n";
    if constexpr (!read_only) {
        w.doc->record_symbol(setsym);
        w.doc->shim += "void " + setsym + "(void* self, " +
                       std::string{wire_param_v<std::meta::type_of(Mem)>} +
                       " v, welder_error* err) { return "
                       "wcs::shim::field_set<" + anchor + ", " + lookup +
                       ">(self, err, v); }\n\n";
        w.doc->pinvoke += "        " + import_attr(is_str) +
                          " internal static partial void " + setsym +
                          "(" + w.handle_cs + " self, " +
                          (is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
                          pinvoke_type<std::meta::type_of(Mem), Style>(false) +
                          " v, out WelderError err);\n";
    }

    // property (the setter's value conversion reuses the parameter
    // machinery — one conversion source for params, setters, operands)
    call_pieces vcp{};
    append_one_param<std::meta::type_of(Mem), Style>(vcp, 0, "value");
    const std::string pname{
        ::welder::name_of<Mem, lang::cs, Style, ::welder::ent_kind::field>()};
    w.surface_names.push_back(pname);
    emit_doc_comment(w.members, "        ", ::welder::doc_of<Mem>());
    constexpr bool unsafe_prop{
        classify(std::meta::type_of(Mem)) == marshal_kind::seq_value ||
        classify(std::meta::type_of(Mem)) == marshal_kind::tuple_value};
    w.members += "        public " +
                 std::string{unsafe_prop ? "unsafe " : ""} +
                 public_type<std::meta::type_of(Mem), Style>() + " " + pname +
                 "\n        {\n            get\n            {\n";
    w.members += wrapper_return_body<std::meta::type_of(Mem), Style,
                                     field_return_policy(
                                         std::meta::type_of(Mem))>(
        "NativeMethods." + getsym + "(" + w.handle_field +
            ", out WelderError _e)",
        "                ", "this");
    w.members += "            }\n";
    if constexpr (!read_only) {
        const std::string sind{vcp.post.empty() ? "                "
                                                : "                    "};
        w.members += "            set\n            {\n" +
                     vcp.wrap(sind + "NativeMethods." + setsym + "(" +
                                  w.handle_field + ", " + vcp.wrapper_args +
                                  ", out WelderError _e);\n" + sind +
                                  "WelderInterop.ThrowIfError(in _e);\n",
                              "                ") +
                     "            }\n";
    }
    w.members += "        }\n\n";
}

/** The live-field binding for a scalar/enum sequence member: the getter
    thunk hands out the MEMBER'S ADDRESS (a view the wrapper pins to its
    parent), the setter — absent under `mark::no_reassign` — copy-assigns
    the whole container from another wrapped instance (the implicit `T[]`
    conversion makes `obj.Nums = new[] {…}` read naturally). */
template <std::meta::info Mem, class Style>
void emit_scalar_seq_field(class_writer& w) {
    constexpr std::meta::info MT{std::meta::type_of(Mem)};
    ensure_scalar_seq<bare(MT)>(*w.doc);
    const std::string id{std::meta::identifier_of(Mem)};
    const std::string anchor{w.cpp_anchor};
    const member_scope ms{member_scope_of<Mem>(w.cpp_qualified, anchor,
                                               w.type_token)};
    const std::string lookup{"wcs::named_field(" + ms.owner + ", \"" + id +
                             "\")"};
    const std::string getsym{w.sym_prefix + "_get_" + id + ms.suffix};
    const std::string setsym{w.sym_prefix + "_set_" + id + ms.suffix};
    constexpr bool read_only{::welder::member_no_reassign(Mem, lang::cs)};
    const std::string V{container_ref<bare(MT)>()};
    w.doc->record_symbol(getsym);
    w.doc->shim += "void* " + getsym +
                   "(void* self, welder_error* err) { return "
                   "wcs::shim::field_addr<" + anchor + ", " + lookup +
                   ">(self, err); }\n\n";
    w.doc->pinvoke += "        [LibraryImport(Lib)] internal static "
                      "partial IntPtr " + getsym + "(" + w.handle_cs +
                      " self, out WelderError err);\n";
    if constexpr (!read_only) {
        w.doc->record_symbol(setsym);
        w.doc->shim += "void " + setsym +
                       "(void* self, void* v, welder_error* err) { "
                       "wcs::shim::field_assign<" + anchor + ", " + lookup +
                       ">(self, v, err); }\n\n";
        w.doc->pinvoke += "        [LibraryImport(Lib)] internal static "
                          "partial void " + setsym + "(" + w.handle_cs +
                          " self, " + V + "Handle v, out WelderError "
                          "err);\n";
    }
    const std::string pname{
        ::welder::name_of<Mem, lang::cs, Style, ::welder::ent_kind::field>()};
    w.surface_names.push_back(pname);
    emit_doc_comment(w.members, "        ", ::welder::doc_of<Mem>());
    w.members += "        public " + V + " " + pname +
                 "\n        {\n            get\n            {\n"
                 "                IntPtr _r = NativeMethods." + getsym +
                 "(" + w.handle_field + ", out WelderError _e);\n"
                 "                WelderInterop.ThrowIfError(in _e);\n"
                 "                var _v = new " + V + "(_r, false);\n"
                 "                _v._owner = this;\n"
                 "                return _v;\n            }\n";
    if constexpr (!read_only)
        w.members += "            set\n            {\n"
                     "                NativeMethods." + setsym + "(" +
                     w.handle_field + ", value._h_" + V +
                     ", out WelderError _e);\n"
                     "                WelderInterop.ThrowIfError(in _e);\n"
                     "            }\n";
    w.members += "        }\n\n";
}

/** One resolved method-backed property: the getter's thunk under a
    property-read symbol, the (optional) setter's under a write symbol, and
    a C# property calling them under the driver-resolved @a name. */
template <class T, std::meta::info Getter, std::meta::info Setter>
void emit_property(class_writer& w, const char* name) {
    w.surface_names.push_back(name);
    collect_containers<Getter>(*w.doc);
    if constexpr (Setter != std::meta::info{})
        collect_containers<Setter>(*w.doc);
    ::welder::validate_return_policy<Getter, lang::cs>();
    constexpr std::meta::info RT{
        std::meta::remove_cvref(std::meta::return_type_of(Getter))};
    constexpr bool checked{(require_marshallable(RT, true), true)};
    static_assert(checked);
    const std::string anchor{w.cpp_anchor};
    const std::string gid{std::meta::identifier_of(Getter)};
    const std::string glookup{
        "wcs::named_member(" +
        member_scope_of<Getter>(w.cpp_qualified, anchor, w.type_token).owner +
        ", \"" + gid + "\", " +
        std::to_string(index_of_named_member(Getter)) + ")"};
    const std::string getsym{w.sym_prefix + "_pget_" + name};
    w.doc->record_symbol(getsym);
    w.doc->shim += std::string{wire_return_v<std::meta::remove_cvref(std::meta::return_type_of(Getter))>} + " " + getsym +
                   "(void* self, welder_error* err) { return "
                   "wcs::shim::method<" + anchor + ", " + glookup +
                   ">(self, err); }\n\n";
    constexpr bool is_bool{classify(RT) == marshal_kind::boolean};
    w.doc->pinvoke += "        " + import_attr(false) + " " +
                      (is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "") +
                      "internal static partial " +
                      pinvoke_type<std::meta::remove_cvref(std::meta::return_type_of(Getter)), ::welder::naming::none>(true) +
                      " " + getsym + "(" + w.handle_cs +
                      " self, out WelderError err);\n";

    emit_doc_comment(w.members, "        ", ::welder::doc_of<Getter>());
    w.members += "        public " +
                 public_type<std::meta::remove_cvref(std::meta::return_type_of(Getter)), ::welder::naming::none>() +
                 " " + name + "\n        {\n            get\n            {\n";
    w.members += wrapper_return_body<std::meta::return_type_of(Getter),
                                     ::welder::naming::none,
                                     ::welder::return_policy_of(
                                         Getter, lang::cs)>(
        "NativeMethods." + getsym + "(" + w.handle_field +
            ", out WelderError _e)",
        "                ", "this");
    w.members += "            }\n";
    if constexpr (Setter != std::meta::info{}) {
        constexpr std::meta::info PT{
            ::welder::detail::param_types<Setter>()[0]};
        constexpr bool pchecked{(require_marshallable(PT, false), true)};
        static_assert(pchecked);
        const std::string sid{std::meta::identifier_of(Setter)};
        const std::string slookup{
            "wcs::named_member(" +
            member_scope_of<Setter>(w.cpp_qualified, anchor, w.type_token)
                .owner +
            ", \"" + sid + "\", " +
            std::to_string(index_of_named_member(Setter)) + ")"};
        const std::string setsym{w.sym_prefix + "_pset_" + name};
        w.doc->record_symbol(setsym);
        // A fluent (non-void) setter return is discarded by shim::method's
        // guarded<void> path? No — the support template returns the wire
        // form of the REAL return type; the thunk discards it by spelling
        // void and an expression statement.
        w.doc->shim += "void " + setsym + "(void* self, " +
                       std::string{wire_param_v<first_param_type(Setter)>} +
                       " v, welder_error* err) { (void)wcs::shim::method<" +
                       anchor + ", " + slookup + ">(self, err, v); }\n\n";
        constexpr bool p_is_bool{classify(PT) == marshal_kind::boolean};
        constexpr bool p_is_str{classify(PT) == marshal_kind::utf8_string};
        w.doc->pinvoke += "        " + import_attr(p_is_str) +
                          " internal static partial void " + setsym +
                          "(" + w.handle_cs + " self, " +
                          (p_is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
                          pinvoke_type<first_param_type(Setter),
                                       ::welder::naming::none>(false) +
                          " v, out WelderError err);\n";
        call_pieces vcp{};
        append_one_param<first_param_type(Setter), ::welder::naming::none>(
            vcp, 0, "value");
        const std::string sind{vcp.post.empty() ? "                "
                                                : "                    "};
        w.members += "            set\n            {\n" +
                     vcp.wrap(sind + "NativeMethods." + setsym + "(" +
                                  w.handle_field + ", " + vcp.wrapper_args +
                                  ", out WelderError _e);\n" + sind +
                                  "WelderInterop.ThrowIfError(in _e);\n",
                              "                ") +
                     "            }\n";
    }
    w.members += "        }\n\n";
}
} // namespace welder::inline v0::rods::csharp
