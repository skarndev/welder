#pragma once
#include <cstddef>
#include <meta>
#include <string>

#include <welder/doc.hpp>
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/callables.hpp>
#include <welder/rods/csharp/emit/containers.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/text.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **Namespace-scope members.** C# has no free functions or namespace
    variables, so each C++ namespace's non-type members land on a `Global`
    static class in the matching C# namespace — the section
    @ref welder::rods::csharp::document::section hands out.

    Otherwise these are ordinary emissions: an overload group becomes static
    overloads, and a variable becomes a static property over a getter (and, when
    it is not const, a setter) thunk.
*/

namespace welder::inline v0::rods::csharp {

/** Emit free-function overload group @a Fns as `public static` overloads on
    the namespace's static class. A non-null @a name overrides the leaf name
    (beating any `weld_as`). */
template <auto Fns, class Style = ::welder::naming::none>
void emit_function_group(module_writer& m, const char* name) {
    const std::string wname{
        ::welder::name_of_or<Fns[0], lang::cs, Style,
                             ::welder::ent_kind::function>(name)};
    template for (constexpr auto fn : std::define_static_array(Fns)) {
        constexpr std::size_t k{index_of_named_member(fn)};
        collect_containers<fn>(*m.doc);
        constexpr std::meta::info Ns{std::meta::parent_of(fn)};
        const std::string id{std::meta::identifier_of(fn)};
        const std::string sym{std::string{"welder_"} + upath_v<Ns> + "_f_" +
                              id + "_" + std::to_string(k)};
        const std::string expr{
            "wcs::shim::function<wcs::named_member(^^" +
            std::string{cpp_name_v<Ns>} + ", \"" + id + "\", " +
            std::to_string(k) + ")>"};
        emit_callable<fn, Style, false>(*m.doc, sym,
                                        m.doc->section(m.cs_ns).statics,
                                        "        ", wname, expr);
    }
}
template <std::meta::info Var, class Style = ::welder::naming::none>
void emit_variable(module_writer& m, const char* name) {
    constexpr std::meta::info VT{std::meta::type_of(Var)};
    ensure_for<std::meta::type_of(Var)>(*m.doc);
    constexpr bool checked{(require_marshallable(VT, true), true)};
    static_assert(checked);
    constexpr std::meta::info Ns{std::meta::parent_of(Var)};
    const std::string id{std::meta::identifier_of(Var)};
    const std::string lookup{"wcs::named_field(^^" +
                             std::string{cpp_name_v<Ns>} + ", \"" + id +
                             "\")"};
    const std::string base{std::string{"welder_"} + upath_v<Ns> + "_v_" + id};
    constexpr bool read_only{std::meta::is_const_type(VT)};
    constexpr bool is_str{classify(VT) == marshal_kind::utf8_string};
    constexpr bool is_bool{classify(VT) == marshal_kind::boolean};
    std::string& body{m.doc->section(m.cs_ns).statics};

    m.doc->record_symbol(base + "_get");
    m.doc->shim += std::string{wire_return_v<std::meta::type_of(Var)>} + " " + base +
                   "_get(welder_error* err) { return wcs::shim::var_get<" +
                   lookup + ">(err); }\n\n";
    m.doc->pinvoke += "        " + import_attr(false) + " " +
                      (is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "") +
                      "internal static partial " +
                      pinvoke_type<std::meta::type_of(Var), Style>(true) + " " + base + "_get(out WelderError err);\n";
    if constexpr (!read_only) {
        m.doc->record_symbol(base + "_set");
        m.doc->shim += "void " + base + "_set(" +
                       std::string{wire_param_v<std::meta::type_of(Var)>} +
                       " v, welder_error* err) { return wcs::shim::var_set<" +
                       lookup + ">(err, v); }\n\n";
        m.doc->pinvoke += "        " + import_attr(is_str) +
                          " internal static partial void " + base + "_set(" +
                          (is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
                          pinvoke_type<std::meta::type_of(Var), Style>(false) +
                          " v, out WelderError err);\n";
    }
    const std::string vname{
        ::welder::name_of_or<Var, lang::cs, Style,
                             ::welder::ent_kind::variable>(name)};
    emit_doc_comment(body, "        ", ::welder::doc_of<Var>());
    constexpr bool unsafe_var{
        classify(std::meta::type_of(Var)) == marshal_kind::seq_value ||
        classify(std::meta::type_of(Var)) == marshal_kind::tuple_value};
    body += "        public static " +
            std::string{unsafe_var ? "unsafe " : ""} +
            public_type<std::meta::type_of(Var), Style>() + " " + vname +
            "\n        {\n            get\n            {\n";
    body += wrapper_return_body<std::meta::type_of(Var), Style>(
        "NativeMethods." + base + "_get(out WelderError _e)",
        "                ");
    body += "            }\n";
    if constexpr (!read_only) {
        call_pieces vcp{};
        append_one_param<std::meta::type_of(Var), Style>(vcp, 0, "value");
        body += "            set\n            {\n" +
                (vcp.pin_open.empty()
                     ? std::string{}
                     : "                " + vcp.pin_open + "{\n") +
                vcp.pre +
                "                NativeMethods." + base + "_set(" +
                vcp.wrapper_args + ", out WelderError _e);\n"
                "                WelderInterop.ThrowIfError(in _e);\n" +
                (vcp.pin_open.empty() ? std::string{}
                                      : "                }\n") +
                "            }\n";
    }
    body += "        }\n\n";
}
} // namespace welder::inline v0::rods::csharp
