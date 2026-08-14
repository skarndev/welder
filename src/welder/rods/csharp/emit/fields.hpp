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
    **Properties**: both kinds. A C# property can come from a data member or
    from an accessor pair the driver resolved; both emit a getter thunk, an
    optional setter thunk, and the managed property calling them.
    @ref welder::rods::csharp::field_emitter is the component.

    The interesting decision is what a **field's read hands out**. A non-const
    class-typed or container-typed member yields a LIVE view tied to its parent
    — the runtime rods' `def_readwrite` reference_internal semantics — so that
    `obj.Items[0].Field = x` writes through. A non-const scalar/enum sequence
    member goes further, to a wrapper with a zero-copy `Span<T>` over the C++
    buffer, rather than the by-value `T[]` snapshot params and returns use — a
    snapshot would make `obj.Nums.Add(…)` silently mutate a temporary. A const
    member, and every leaf kind, crosses by value.
*/

namespace welder::inline v0::rods::csharp {

/** The component that emits a class's property surface: data members
    (@ref emit_field) and driver-resolved accessor pairs (@ref emit_property).

    Both paths share one shape — a getter thunk, an optional setter thunk, and
    a managed property over them — and both route the setter's inbound value
    conversion through @ref append_one_param, the single conversion source
    parameters, setters, operands and map keys/values all use. */
class field_emitter {
  public:
    /** Bind the emitter to class handle @a w.
        @param w the class being emitted into. */
    explicit field_emitter(class_writer& w) : _writer{w} {}

    /** Bind data member @a Mem as a C# property. A non-const scalar/enum
        sequence member routes to the live-wrapper form (@ref
        emit_scalar_seq); everything else emits `field_get`/`field_set` thunks
        and a by-value (or live-view, for handle kinds) property.
        @tparam Mem   a reflection of the data member.
        @tparam Style the name style. */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    void emit_field() {
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
            emit_scalar_seq<Mem, Style>();
            return;
        }
        ensure_for<MT>(*_writer.doc);
        constexpr bool checked{(require_marshallable(MT, true), true)};
        static_assert(checked);
        const std::string id{std::meta::identifier_of(Mem)};
        // The lookup is by the DECLARING scope (a flattened non-welded base's
        // member is not among the welded type's own members), and a flattened
        // member's symbol is namespaced by that scope — a base field shadowed
        // by a derived one of the same name would otherwise emit two identical
        // symbols.
        const member_scope ms{member_scope_of<Mem>(_writer.cpp_qualified,
                                                   _writer.cpp_anchor,
                                                   _writer.type_token)};
        const std::string lookup{"wcs::named_field(" + ms.owner + ", \"" + id +
                                 "\")"};
        constexpr bool read_only{std::meta::is_const_type(MT) ||
                                 ::welder::member_no_reassign(Mem, lang::cs)};
        constexpr bool is_str{classify(MT) == marshal_kind::utf8_string};
        constexpr bool is_bool{classify(MT) == marshal_kind::boolean};

        // getter thunk + P/Invoke
        const bound_symbol get{*_writer.doc, _writer.sym_prefix + "_get_" + id + ms.suffix,
                               _writer.members, 2};
        code_writer t{get.thunk()};
        t.line("{} {}(void* self, welder_error* err) { return "
               "wcs::shim::field_get<{}, {}>(self, err); }",
               wire_return_v<MT>, get.name(), _writer.cpp_anchor, lookup);
        t.blank();
        get.pinvoke().line(
            "{} {}internal static partial {} {}({} self, out WelderError err);",
            import_attr(false),
            is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "",
            pinvoke_type<MT, Style>(true), get.name(), _writer.handle_cs);
        std::string setsym{};
        if constexpr (!read_only) {
            const bound_symbol set{*_writer.doc,
                                   _writer.sym_prefix + "_set_" + id + ms.suffix,
                                   _writer.members, 2};
            setsym = set.name();
            code_writer st{set.thunk()};
            st.line("void {}(void* self, {} v, welder_error* err) { return "
                    "wcs::shim::field_set<{}, {}>(self, err, v); }",
                    set.name(), wire_param_v<MT>, _writer.cpp_anchor, lookup);
            st.blank();
            set.pinvoke().line(
                "{} internal static partial void {}({} self, {}{} v, out "
                "WelderError err);",
                import_attr(is_str), set.name(), _writer.handle_cs,
                is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "",
                pinvoke_type<MT, Style>(false));
        }

        // property (the setter's value conversion reuses the parameter
        // machinery — one conversion source for params, setters, operands)
        call_pieces vcp{};
        append_one_param<MT, Style>(vcp, 0, "value");
        const std::string pname{
            ::welder::name_of<Mem, lang::cs, Style,
                              ::welder::ent_kind::field>()};
        _writer.surface_names.push_back(pname);
        emit_doc_comment(_writer.members, "        ", ::welder::doc_of<Mem>());
        constexpr bool unsafe_prop{classify(MT) == marshal_kind::seq_value ||
                                   classify(MT) == marshal_kind::tuple_value};
        code_writer mw{get.wrapper()};
        mw.line("public {}{} {}", unsafe_prop ? "unsafe " : "",
                public_type<MT, Style>(), pname);
        {
            const auto prop{mw.braces()};
            mw.line("get");
            {
                const auto arm{mw.braces()};
                mw.raw(wrapper_return_body<MT, Style,
                                           field_return_policy(MT)>(
                    "NativeMethods." + get.name() + "(" + _writer.handle_field +
                        ", out WelderError _e)",
                    mw.indentation(), "this"));
            }
            if constexpr (!read_only)
                emit_set_arm(mw, vcp,
                             "NativeMethods." + setsym + "(" +
                                 _writer.handle_field + ", " + vcp.wrapper_args +
                                 ", out WelderError _e);");
        }
        mw.blank();
    }

    /** One resolved method-backed property: the getter's thunk under a
        property-read symbol, the (optional) setter's under a write symbol, and
        a C# property calling them under the driver-resolved @a name.
        @tparam T      the welded class.
        @tparam Getter a reflection of the resolved getter (const, 0-param).
        @tparam Setter a reflection of the resolved setter, or a null
                       reflection for a read-only property.
        @param name the driver-resolved property name (verbatim). */
    template <class T, std::meta::info Getter, std::meta::info Setter>
    void emit_property(const char* name) {
        _writer.surface_names.push_back(name);
        collect_containers<Getter>(*_writer.doc);
        if constexpr (Setter != std::meta::info{})
            collect_containers<Setter>(*_writer.doc);
        ::welder::validate_return_policy<Getter, lang::cs>();
        constexpr std::meta::info RT{
            std::meta::remove_cvref(std::meta::return_type_of(Getter))};
        constexpr bool checked{(require_marshallable(RT, true), true)};
        static_assert(checked);
        const std::string gid{std::meta::identifier_of(Getter)};
        const std::string glookup{
            "wcs::named_member(" +
            member_scope_of<Getter>(_writer.cpp_qualified, _writer.cpp_anchor,
                                    _writer.type_token)
                .owner +
            ", \"" + gid + "\", " +
            std::to_string(index_of_named_member(Getter)) + ")"};
        const bound_symbol get{*_writer.doc, _writer.sym_prefix + "_pget_" + name,
                               _writer.members, 2};
        code_writer t{get.thunk()};
        t.line("{} {}(void* self, welder_error* err) { return "
               "wcs::shim::method<{}, {}>(self, err); }",
               wire_return_v<RT>, get.name(), _writer.cpp_anchor, glookup);
        t.blank();
        constexpr bool is_bool{classify(RT) == marshal_kind::boolean};
        get.pinvoke().line(
            "{} {}internal static partial {} {}({} self, out WelderError err);",
            import_attr(false),
            is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "",
            pinvoke_type<RT, ::welder::naming::none>(true), get.name(),
            _writer.handle_cs);

        emit_doc_comment(_writer.members, "        ", ::welder::doc_of<Getter>());
        code_writer mw{get.wrapper()};
        mw.line("public {} {}", public_type<RT, ::welder::naming::none>(),
                name);
        {
            const auto prop{mw.braces()};
            mw.line("get");
            {
                const auto arm{mw.braces()};
                mw.raw(wrapper_return_body<std::meta::return_type_of(Getter),
                                           ::welder::naming::none,
                                           ::welder::return_policy_of(
                                               Getter, lang::cs)>(
                    "NativeMethods." + get.name() + "(" + _writer.handle_field +
                        ", out WelderError _e)",
                    mw.indentation(), "this"));
            }
            if constexpr (Setter != std::meta::info{})
                emit_property_setter<Setter>(mw, name);
        }
        mw.blank();
    }

  private:
    /** The live-field binding for a scalar/enum sequence member: the getter
        thunk hands out the MEMBER'S ADDRESS (a view the wrapper pins to its
        parent), the setter — absent under `mark::no_reassign` — copy-assigns
        the whole container from another wrapped instance (the implicit `T[]`
        conversion makes `obj.Nums = new[] {…}` read naturally).
        @tparam Mem   a reflection of the sequence data member.
        @tparam Style the name style. */
    template <std::meta::info Mem, class Style>
    void emit_scalar_seq() {
        constexpr std::meta::info MT{std::meta::type_of(Mem)};
        ensure_scalar_seq<bare(MT)>(*_writer.doc);
        const std::string id{std::meta::identifier_of(Mem)};
        const member_scope ms{member_scope_of<Mem>(_writer.cpp_qualified,
                                                   _writer.cpp_anchor,
                                                   _writer.type_token)};
        const std::string lookup{"wcs::named_field(" + ms.owner + ", \"" + id +
                                 "\")"};
        constexpr bool read_only{::welder::member_no_reassign(Mem, lang::cs)};
        const std::string V{container_ref<bare(MT)>()};
        const bound_symbol get{*_writer.doc, _writer.sym_prefix + "_get_" + id + ms.suffix,
                               _writer.members, 2};
        code_writer t{get.thunk()};
        t.line("void* {}(void* self, welder_error* err) { return "
               "wcs::shim::field_addr<{}, {}>(self, err); }",
               get.name(), _writer.cpp_anchor, lookup);
        t.blank();
        get.pinvoke().line(
            "[LibraryImport(Lib)] internal static partial IntPtr {}({} self, "
            "out WelderError err);",
            get.name(), _writer.handle_cs);
        std::string setsym{};
        if constexpr (!read_only) {
            const bound_symbol set{*_writer.doc,
                                   _writer.sym_prefix + "_set_" + id + ms.suffix,
                                   _writer.members, 2};
            setsym = set.name();
            code_writer st{set.thunk()};
            st.line("void {}(void* self, void* v, welder_error* err) { "
                    "wcs::shim::field_assign<{}, {}>(self, v, err); }",
                    set.name(), _writer.cpp_anchor, lookup);
            st.blank();
            set.pinvoke().line(
                "[LibraryImport(Lib)] internal static partial void {}({} "
                "self, {}Handle v, out WelderError err);",
                set.name(), _writer.handle_cs, V);
        }
        const std::string pname{
            ::welder::name_of<Mem, lang::cs, Style,
                              ::welder::ent_kind::field>()};
        _writer.surface_names.push_back(pname);
        emit_doc_comment(_writer.members, "        ", ::welder::doc_of<Mem>());
        code_writer mw{get.wrapper()};
        mw.line("public {} {}", V, pname);
        {
            const auto prop{mw.braces()};
            mw.line("get");
            {
                const auto arm{mw.braces()};
                mw.line("IntPtr _r = NativeMethods.{}({}, out WelderError _e);",
                        get.name(), _writer.handle_field);
                mw.line("WelderInterop.ThrowIfError(in _e);");
                mw.line("var _v = new {}(_r, false);", V);
                mw.line("_v._owner = this;");
                mw.line("return _v;");
            }
            if constexpr (!read_only) {
                mw.line("set");
                {
                    const auto arm{mw.braces()};
                    mw.line("NativeMethods.{}({}, value._h_{}, out "
                            "WelderError _e);",
                            setsym, _writer.handle_field, V);
                    mw.line("WelderInterop.ThrowIfError(in _e);");
                }
            }
        }
        mw.blank();
    }

    /** The method-backed property's write half: the setter's thunk +
        P/Invoke + the `set` arm (the value conversion through
        @ref append_one_param, like a field's).
        @tparam Setter a reflection of the resolved setter (exactly 1 param).
        @param mw   the property's wrapper writer (inside the property braces).
        @param name the property's C# name (the write symbol derives from it). */
    template <std::meta::info Setter>
    void emit_property_setter(code_writer& mw, const char* name) {
        constexpr std::meta::info PT{::welder::detail::param_types<Setter>()[0]};
        constexpr bool pchecked{(require_marshallable(PT, false), true)};
        static_assert(pchecked);
        const std::string sid{std::meta::identifier_of(Setter)};
        const std::string slookup{
            "wcs::named_member(" +
            member_scope_of<Setter>(_writer.cpp_qualified, _writer.cpp_anchor,
                                    _writer.type_token)
                .owner +
            ", \"" + sid + "\", " +
            std::to_string(index_of_named_member(Setter)) + ")"};
        const bound_symbol set{*_writer.doc, _writer.sym_prefix + "_pset_" + name,
                               _writer.members, 2};
        // A fluent (non-void) setter return is discarded here — the support
        // template returns the wire form of the REAL return type; the thunk
        // discards it by spelling void and an expression statement.
        code_writer st{set.thunk()};
        st.line("void {}(void* self, {} v, welder_error* err) { "
                "(void)wcs::shim::method<{}, {}>(self, err, v); }",
                set.name(), wire_param_v<first_param_type(Setter)>,
                _writer.cpp_anchor, slookup);
        st.blank();
        constexpr bool p_is_bool{classify(PT) == marshal_kind::boolean};
        constexpr bool p_is_str{classify(PT) == marshal_kind::utf8_string};
        set.pinvoke().line(
            "{} internal static partial void {}({} self, {}{} v, out "
            "WelderError err);",
            import_attr(p_is_str), set.name(), _writer.handle_cs,
            p_is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "",
            pinvoke_type<first_param_type(Setter), ::welder::naming::none>(
                false));
        call_pieces vcp{};
        append_one_param<first_param_type(Setter), ::welder::naming::none>(
            vcp, 0, "value");
        emit_set_arm(mw, vcp,
                     "NativeMethods." + set.name() + "(" + _writer.handle_field +
                         ", " + vcp.wrapper_args + ", out WelderError _e);");
    }

    class_writer& _writer; /**< The class being emitted into. */
};

} // namespace welder::inline v0::rods::csharp
