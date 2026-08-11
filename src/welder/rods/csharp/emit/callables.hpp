#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <vector>

#include <welder/doc.hpp>                         // doc_of / param_docs / return_doc_of
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/text.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **The three-way emission primitive.** Every bound callable — method, static
    method, free function, operator, constructor — is written out as the same
    triple, keyed by one C symbol: a native thunk (into the document's shim
    buffer), a `[LibraryImport]` declaration (into its P/Invoke buffer), and a
    managed wrapper (into whichever body the caller passes).

    Emitting them together, from one place, is what keeps the two artifacts in
    lockstep: there is no way to add a thunk and forget its declaration, and a
    colliding symbol is caught centrally by
    @ref welder::rods::csharp::document::record_symbol.
*/

namespace welder::inline v0::rods::csharp {

/** Emit the XML doc block for callable @a Fn above its wrapper: `<summary>` from
    its `[[=welder::doc]]`, one `<param>` per documented parameter (keyed by the
    C# parameter names in @a cp) and `<returns>` from `[[=welder::returns]]`. */
template <std::meta::info Fn>
void emit_callable_docs(std::string& out, const std::string& ind,
                        const call_pieces& cp) {
    emit_doc_comment(out, ind, ::welder::doc_of<Fn>());
    static constexpr auto pds{::welder::param_docs<Fn>()};
    if constexpr (pds.size() != 0) {
        const std::vector<std::string> names{split_param_names(cp.param_names)};
        for (std::size_t i{0}; i < pds.size(); ++i) {
            if (!pds[i].text || !*pds[i].text)
                continue;
            const std::string name{
                i < names.size()
                    ? names[i]
                    : std::string{pds[i].name ? pds[i].name : ""}};
            out += ind + "/// <param name=\"" + name + "\">" +
                   one_line(pds[i].text) + "</param>\n";
        }
    }
    if (const char* r{::welder::return_doc_of<Fn>()}; r && *r)
        out += ind + "/// <returns>" + one_line(r) + "</returns>\n";
}

/** Emit one callable overload: its shim thunk (a one-line delegation into
    `shim_support.hpp`, into `doc.shim`), its P/Invoke declaration (into
    `doc.pinvoke`) and its wrapper (into @a wrapper_out).

    @tparam HasSelf true for an instance entity (a leading `void* self` /
                    `_handle`); false for a static method or free function.
    @param delegate_expr the support-template instantiation text, e.g.
           `wcs::shim::method<^^::geo::Point, wcs::named_member(^^::geo::Point,
           "area", 0)>` — the thunk body appends `(self, err, a0, …)`. */
template <std::meta::info Fn, class Style, bool HasSelf>
void emit_callable(document& doc, const std::string& sym, std::string& wrapper_out,
                   const std::string& indent, const std::string& wrapper_name,
                   const std::string& delegate_expr,
                   const std::string& self_cs = {},
                   const std::string& self_field = {}) {
    ::welder::validate_return_policy<Fn, lang::cs>();
    constexpr std::meta::info R{std::meta::return_type_of(Fn)};
    constexpr bool ret_checked{(require_marshallable(R, true), true)};
    static_assert(ret_checked);
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    const call_pieces cp{build_params<Fn, Style>(std::make_index_sequence<n>{})};
    doc.record_symbol(sym);

    // --- shim thunk (a delegation one-liner) --------------------------------
    std::string shim_params{cp.shim_params};
    if constexpr (HasSelf)
        shim_params = "void* self" +
                      (cp.shim_params.empty() ? "" : ", " + cp.shim_params);
    shim_params += (shim_params.empty() ? "" : ", ");
    shim_params += "welder_error* err";
    std::string delegate_args{HasSelf ? "self, err" : "err"};
    if (!cp.delegate_args.empty())
        delegate_args += ", " + cp.delegate_args;
    doc.shim += std::string{wire_return_v<std::meta::return_type_of(Fn)>} + " " + sym + "(" + shim_params +
                ") { return " + delegate_expr + "(" + delegate_args + "); }\n\n";

    // --- P/Invoke declaration ----------------------------------------------
    std::string pin_params{cp.pinvoke_params};
    if constexpr (HasSelf)
        pin_params = self_cs + " self" +
                     (cp.pinvoke_params.empty() ? "" : ", " + cp.pinvoke_params);
    pin_params += (pin_params.empty() ? "" : ", ");
    pin_params += "out WelderError err";
    constexpr bool r_is_bool{classify(R) == marshal_kind::boolean};
    const std::string ret_attr{r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] "
                                         : ""};
    doc.pinvoke += "        " + import_attr(cp.has_string) + " " + ret_attr +
                   "internal static partial " +
                   pinvoke_type<std::meta::return_type_of(Fn), Style>(true) + " " +
                   sym + "(" + pin_params + ");\n";

    // --- managed wrapper ----------------------------------------------------
    std::string call_args{HasSelf ? self_field : std::string{}};
    if (!cp.wrapper_args.empty())
        call_args += (call_args.empty() ? "" : ", ") + cp.wrapper_args;
    call_args += (call_args.empty() ? "" : ", ");
    call_args += "out WelderError _e";
    const std::string pc{"NativeMethods." + sym + "(" + call_args + ")"};
    emit_callable_docs<Fn>(wrapper_out, indent, cp);
    constexpr bool ret_unsafe{
        classify(std::meta::return_type_of(Fn)) == marshal_kind::seq_value ||
        classify(std::meta::return_type_of(Fn)) == marshal_kind::tuple_value};
    const bool is_unsafe{cp.needs_unsafe || ret_unsafe};
    wrapper_out += indent + "public " + (is_unsafe ? "unsafe " : "") +
                   (HasSelf ? "" : "static ") +
                   public_return_type<std::meta::return_type_of(Fn), Style>() +
                   " " + wrapper_name + "(" + cp.wrapper_params + ")\n" + indent +
                   "{\n";
    if (!cp.pin_open.empty())
        wrapper_out += indent + "    " + cp.pin_open + "{\n";
    wrapper_out += cp.pre;
    wrapper_out += wrapper_return_body<std::meta::return_type_of(Fn), Style,
                                       ::welder::return_policy_of(Fn, lang::cs)>(
        pc, indent + "    ", HasSelf ? "this" : "");
    if (!cp.pin_open.empty())
        wrapper_out += indent + "    }\n";
    wrapper_out += indent + "}\n\n";
}

/** Emit a constructor from its parameter pieces: the shim delegation, an
    `IntPtr` P/Invoke, and a `public T(...)` wrapper. */
inline void emit_ctor(class_writer& w, const call_pieces& cp,
                      const std::string& sym, const std::string& delegate_expr) {
    w.doc->record_symbol(sym);
    std::string shim_params{cp.shim_params};
    shim_params += (shim_params.empty() ? "" : ", ");
    shim_params += "welder_error* err";
    std::string delegate_args{"err"};
    if (!cp.delegate_args.empty())
        delegate_args += ", " + cp.delegate_args;
    w.doc->shim += "void* " + sym + "(" + shim_params + ") { return " +
                   delegate_expr + "(" + delegate_args + "); }\n\n";
    std::string pin_params{cp.pinvoke_params};
    pin_params += (pin_params.empty() ? "" : ", ");
    pin_params += "out WelderError err";
    w.doc->pinvoke += "        " + import_attr(cp.has_string) +
                      " internal static partial IntPtr " + sym + "(" +
                      pin_params + ");\n";
    // The public constructor CHAINS through the internal (IntPtr, bool) one
    // (which initializes every base level's upcast handle), so construction
    // works identically for roots and derived classes. The static helper
    // exists because a chained `this(...)` argument cannot use `out var`.
    const std::string helper{"_New" + sym.substr(sym.rfind("_new") + 4)};
    w.members += "        private static " +
                 std::string{cp.needs_unsafe ? "unsafe " : ""} + "IntPtr " +
                 helper + "(" + cp.wrapper_params + ")\n        {\n" +
                 (cp.pin_open.empty() ? "" : "            " + cp.pin_open +
                                             "{\n") +
                 cp.pre +
                 "            IntPtr _r = NativeMethods." + sym + "(" +
                 (cp.wrapper_args.empty() ? std::string{}
                                          : cp.wrapper_args + ", ") +
                 "out WelderError _e);\n"
                 "            WelderInterop.ThrowIfError(in _e);\n"
                 "            return _r;\n" +
                 (cp.pin_open.empty() ? "" : "            }\n") +
                 "        }\n";
    // Re-list the wrapper parameter NAMES for the chained call.
    std::string names{};
    for (const std::string& n : split_param_names(cp.param_names)) {
        names += (names.empty() ? "" : ", ");
        names += n;
    }
    w.members += "        public " + w.cs_name + "(" + cp.wrapper_params +
                 ") : this(" + helper + "(" + names + "), true) {" +
                 (w.is_director ? " _DirBind(); " : "") + "}\n\n";
}
} // namespace welder::inline v0::rods::csharp
