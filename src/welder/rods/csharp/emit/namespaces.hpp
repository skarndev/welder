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
    @ref welder::rods::csharp::namespace_emitter is the component.

    Otherwise these are ordinary emissions: an overload group becomes static
    overloads (through @ref welder::rods::csharp::callable_emitter), and a
    variable becomes a static property over a getter (and, when it is not
    const, a setter) thunk.
*/

namespace welder::inline v0::rods::csharp {

/** The component that emits a namespace's non-type members onto its `Global`
    static class: free-function overload groups (@ref emit_function_group) and
    namespace variables as static properties (@ref emit_variable). */
class namespace_emitter {
  public:
    /** Bind the emitter to module handle @a m (one C# namespace's slice).
        @param m the module handle. */
    explicit namespace_emitter(module_writer& m) : _module{m} {}

    /** Emit free-function overload group @a Fns as `public static` overloads
        on the namespace's static class.
        @tparam Fns   the carriage-computed overload group.
        @tparam Style the name style.
        @param name an optional verbatim name override (beats any `weld_as`),
               or null for the resolved leaf name. */
    template <auto Fns, class Style = ::welder::naming::none>
    void emit_function_group(const char* name) {
        const std::string wname{
            ::welder::name_of_or<Fns[0], lang::cs, Style,
                                 ::welder::ent_kind::function>(name)};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            constexpr std::size_t k{index_of_named_member(fn)};
            collect_containers<fn>(*_module.doc);
            constexpr std::meta::info Ns{std::meta::parent_of(fn)};
            const std::string id{std::meta::identifier_of(fn)};
            const std::string sym{std::string{"welder_"} + upath_v<Ns> +
                                  "_f_" + id + "_" + std::to_string(k)};
            const std::string expr{
                "wcs::shim::function<wcs::named_member(^^" +
                std::string{cpp_name_v<Ns>} + ", \"" + id + "\", " +
                std::to_string(k) + ")>"};
            callable_emitter<fn, Style>{
                bound_symbol{*_module.doc, sym, _module.doc->section(_module.cs_ns).statics,
                             2},
                wname, expr}
                .emit();
        }
    }

    /** Emit namespace variable @a Var as a static property on the namespace's
        static class: a `var_get` thunk (and, non-const, a `var_set` thunk)
        plus the property calling them. A const variable binds read-only (a
        SNAPSHOT would be a lie for the Python rods' live-property model, but
        here every read already crosses the wire).
        @tparam Var   a reflection of the namespace variable.
        @tparam Style the name style.
        @param name an optional verbatim name override, or null. */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    void emit_variable(const char* name) {
        constexpr std::meta::info VT{std::meta::type_of(Var)};
        ensure_for<VT>(*_module.doc);
        constexpr bool checked{(require_marshallable(VT, true), true)};
        static_assert(checked);
        constexpr std::meta::info Ns{std::meta::parent_of(Var)};
        const std::string id{std::meta::identifier_of(Var)};
        const std::string lookup{"wcs::named_field(^^" +
                                 std::string{cpp_name_v<Ns>} + ", \"" + id +
                                 "\")"};
        const std::string base{std::string{"welder_"} + upath_v<Ns> + "_v_" +
                               id};
        constexpr bool read_only{std::meta::is_const_type(VT)};
        constexpr bool is_str{classify(VT) == marshal_kind::utf8_string};
        constexpr bool is_bool{classify(VT) == marshal_kind::boolean};
        std::string& body{_module.doc->section(_module.cs_ns).statics};

        const bound_symbol get{*_module.doc, base + "_get", body, 2};
        code_writer t{get.thunk()};
        t.line("{} {}(welder_error* err) { return wcs::shim::var_get<{}>(err); "
               "}",
               wire_return_v<VT>, get.name(), lookup);
        t.blank();
        get.pinvoke().line(
            "{} {}internal static partial {} {}(out WelderError err);",
            import_attr(false),
            is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "",
            pinvoke_type<VT, Style>(true), get.name());
        std::string setsym{};
        if constexpr (!read_only) {
            const bound_symbol set{*_module.doc, base + "_set", body, 2};
            setsym = set.name();
            code_writer st{set.thunk()};
            st.line("void {}({} v, welder_error* err) { return "
                    "wcs::shim::var_set<{}>(err, v); }",
                    set.name(), wire_param_v<VT>, lookup);
            st.blank();
            set.pinvoke().line(
                "{} internal static partial void {}({}{} v, out WelderError "
                "err);",
                import_attr(is_str), set.name(),
                is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "",
                pinvoke_type<VT, Style>(false));
        }
        const std::string vname{
            ::welder::name_of_or<Var, lang::cs, Style,
                                 ::welder::ent_kind::variable>(name)};
        emit_doc_comment(body, "        ", ::welder::doc_of<Var>());
        constexpr bool unsafe_var{classify(VT) == marshal_kind::seq_value ||
                                  classify(VT) == marshal_kind::tuple_value};
        code_writer mw{get.wrapper()};
        mw.line("public static {}{} {}", unsafe_var ? "unsafe " : "",
                public_type<VT, Style>(), vname);
        {
            const auto prop{mw.braces()};
            mw.line("get");
            {
                const auto arm{mw.braces()};
                mw.raw(wrapper_return_body<VT, Style>(
                    "NativeMethods." + get.name() + "(out WelderError _e)",
                    mw.indentation()));
            }
            if constexpr (!read_only) {
                call_pieces vcp{};
                append_one_param<VT, Style>(vcp, 0, "value");
                emit_set_arm(mw, vcp,
                             "NativeMethods." + setsym + "(" +
                                 vcp.wrapper_args + ", out WelderError _e);");
            }
        }
        mw.blank();
    }

  private:
    module_writer& _module; /**< The module handle (this namespace's slice). */
};

} // namespace welder::inline v0::rods::csharp
