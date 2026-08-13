#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <utility>

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
    @ref welder::rods::csharp::method_emitter is the component.

    An **overridable virtual slot** of a director-eligible type is the one shape
    that needs more than a thunk: it emits as `public virtual` and dispatches by
    origin. A director-constructed instance takes a *qualified base-call* thunk —
    C# has already resolved the dynamic dispatch, and this is also what makes
    `base.Method()` inside an override terminate — while a C++-originated object
    takes the ordinary virtual thunk, since its dynamic type may be a C++
    subclass the managed side has never heard of.
*/

namespace welder::inline v0::rods::csharp {

/** The component that emits a class's method surface: instance overload
    groups (@ref emit_group), static overload groups (@ref emit_static_group)
    and the swept free ostream inserter as `ToString()`
    (@ref emit_stringifier).

    One emitter is constructed per driver hook over the class handle; the
    group's resolved C# name and the class identities live on the object, so
    the per-overload steps take only what genuinely varies per overload. */
class method_emitter {
  public:
    /** Bind the emitter to class handle @a w.
        @param w the class being emitted into. */
    explicit method_emitter(class_writer& w) : w_{w} {}

    /** Emit method overload group @a Fns as natural C# overloads sharing one
        name, each with its own indexed symbol (the group's name resolves from
        `Fns[0]`; the index re-derives the exact overload shim-side). An
        overridable virtual slot of a director-eligible type routes to
        @ref emit_virtual instead of the plain triple.
        @tparam Fns   the carriage-computed overload group (a
                      `std::array<std::meta::info, N>`).
        @tparam Style the name style. */
    template <auto Fns, class Style = ::welder::naming::none>
    void emit_group() {
        name_ = ::welder::name_of<Fns[0], lang::cs, Style,
                                  ::welder::ent_kind::method>();
        w_.surface_names.push_back(name_);
        const std::string anchor{w_.cpp_anchor};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            constexpr std::size_t k{index_of_named_member(fn)};
            const std::string id{std::meta::identifier_of(fn)};
            const member_scope ms{member_scope_of<fn>(w_.cpp_qualified, anchor,
                                                      w_.type_token)};
            const std::string sym{w_.sym_prefix + "_m_" + id + "_" +
                                  std::to_string(k) + ms.suffix};
            const std::string expr{"wcs::shim::method<" + anchor +
                                   ", wcs::named_member(" + ms.owner + ", \"" +
                                   id + "\", " + std::to_string(k) + ")>"};
            static constexpr const char* fsig{std::define_static_string(
                std::meta::display_string_of(std::meta::type_of(fn)))};
            // The director scaffolding names slots through render-time
            // placeholders keyed by (declaring class, identifier, signature) —
            // record every bound method, so an inherited slot resolves through
            // the BASE wrapper's binding.
            w_.doc->record_type_name(
                std::string{cpp_name_v<std::meta::parent_of(fn)>} + "#" + id +
                    "#" + fsig,
                name_);
            collect_containers<fn>(*w_.doc);
            std::size_t vslot_k{static_cast<std::size_t>(-1)};
            if constexpr (std::meta::is_virtual(fn)) {
                if (w_.is_director) {
                    static constexpr const char* vid{
                        std::define_static_string(
                            std::meta::identifier_of(fn))};
                    for (const auto& vs : w_.vslots)
                        if (std::string_view{vs.name} == vid &&
                            std::string_view{vs.sig} == fsig)
                            vslot_k = vs.k;
                }
            }
            if (vslot_k != static_cast<std::size_t>(-1)) {
                emit_virtual<fn, Style>(sym, expr, vslot_k);
            } else {
                callable_emitter<fn, Style>{
                    bound_symbol{*w_.doc, sym, w_.members, 2}, name_, expr,
                    w_.handle_cs, w_.handle_field}
                    .emit();
            }
        }
    }

    /** Emit static-method overload group @a Fns as `public static` overloads.
        @tparam Fns   the carriage-computed overload group.
        @tparam Style the name style. */
    template <auto Fns, class Style = ::welder::naming::none>
    void emit_static_group() {
        name_ = ::welder::name_of<Fns[0], lang::cs, Style,
                                  ::welder::ent_kind::static_method>();
        w_.surface_names.push_back(name_);
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            constexpr std::size_t k{index_of_named_member(fn)};
            collect_containers<fn>(*w_.doc);
            const std::string id{std::meta::identifier_of(fn)};
            const std::string sym{w_.sym_prefix + "_s_" + id + "_" +
                                  std::to_string(k)};
            constexpr bool named_parent{spellable(std::meta::parent_of(fn))};
            const std::string expr{
                "wcs::shim::function<wcs::named_member(" +
                owner_expr(named_parent, cpp_name_v<std::meta::parent_of(fn)>,
                           w_.cpp_qualified, w_.cpp_anchor) +
                ", \"" + id + "\", " + std::to_string(k) + ")>"};
            callable_emitter<fn, Style>{
                bound_symbol{*w_.doc, sym, w_.members, 2}, name_, expr}
                .emit();
        }
    }

    /** Bind the swept free ostream inserter @a Fn as `ToString()` (via
        @ref welder::detail::stringify, dup'd across the wire).
        @tparam T  the welded class.
        @tparam Fn a reflection of the free `operator<<`. */
    template <class T, std::meta::info Fn>
    void emit_stringifier() {
        bound_symbol sym{*w_.doc, w_.sym_prefix + "_str", w_.members, 2};
        constexpr std::size_t k{index_of_operator(Fn)};
        static constexpr const char* opid{std::define_static_string(
            operator_enum_ident(std::meta::operator_of(Fn)))};
        code_writer t{sym.thunk()};
        t.line("const char* {}(void* self, welder_error* err) { return "
               "wcs::shim::stringify_text<{}, wcs::named_operator(^^{}, "
               "std::meta::operators::{}, false, {})>(self, err); }",
               sym.name(), w_.cpp_anchor,
               std::string_view{cpp_name_v<std::meta::parent_of(Fn)>}, opid, k);
        t.blank();
        sym.pinvoke().line(
            "[LibraryImport(Lib)] internal static partial IntPtr {}({} self, "
            "out WelderError err);",
            sym.name(), w_.handle_cs);
        code_writer mw{sym.wrapper()};
        mw.line("public override string ToString()");
        {
            const auto body{mw.braces()};
            mw.raw(wrapper_return_body<^^std::string, ::welder::naming::none>(
                "NativeMethods." + sym.name() + "(" + w_.handle_field +
                    ", out WelderError _e)",
                mw.indentation()));
        }
        mw.blank();
    }

  private:
    /** Emit virtual slot @a Fn: the ordinary (virtual-dispatch) thunk, a
        qualified base-call thunk, P/Invokes for both, and one `public virtual`
        wrapper branching on `_isDirector`. The slot's C# name was already
        recorded for the director scaffolding's placeholders by
        @ref emit_group.
        @tparam Fn    a reflection of the virtual member function.
        @tparam Style the name style (parameter identifiers).
        @param sym  the slot's ordinary C symbol (`…_base` derives from it).
        @param expr the ordinary thunk's delegate expression.
        @param slot the slot's index in the type's overridable-slot set. */
    template <std::meta::info Fn, class Style>
    void emit_virtual(const std::string& sym, const std::string& expr,
                      std::size_t slot) {
        const std::string ks{std::to_string(slot)};

        ::welder::validate_return_policy<Fn, lang::cs>();
        constexpr bool ret_checked{
            (require_marshallable(std::meta::return_type_of(Fn), true), true)};
        static_assert(ret_checked);
        constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
        const call_pieces cp{
            build_params<Fn, Style>(std::make_index_sequence<n>{})};
        const bound_symbol bs{*w_.doc, sym, w_.members, 2};
        const bound_symbol bs_base{*w_.doc, sym + "_base", w_.members, 2};

        const std::string idx{"wcs::director_slot(^^" + w_.cpp_qualified +
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
        code_writer t{bs.thunk()};
        t.line("{} {}({}) { return {}({}); }",
               wire_return_v<std::meta::return_type_of(Fn)>, bs.name(),
               shim_params, expr, delegate_args);
        t.blank();
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
            code_writer bt{bs_base.thunk()};
            bt.line("{} {}({}) {",
                    wire_return_v<std::meta::return_type_of(Fn)>,
                    bs_base.name(), shim_params);
            bt.deeper().line("auto* _o = static_cast<{}*>(self);",
                             w_.cpp_qualified);
            bt.deeper().line(
                "return wcs::shim::guarded<::std::meta::return_type_of({})>"
                "(err, [&]() -> decltype(auto) { return _o->{}::{}({}); });",
                idx, w_.cpp_qualified, std::string_view{fid}, conv);
            bt.line("}");
            bt.blank();
        }
        // P/Invokes for both
        std::string pin_params{w_.handle_cs + " self" +
                               (cp.pinvoke_params.empty()
                                    ? std::string{}
                                    : ", " + cp.pinvoke_params) +
                               ", out WelderError err"};
        constexpr bool r_is_bool{
            classify(std::meta::return_type_of(Fn)) ==
            marshal_kind::boolean};
        for (const bound_symbol* s2 : {&bs, &bs_base})
            s2->pinvoke().line(
                "{} {}internal static partial {} {}({});",
                import_attr(cp.has_string),
                r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "",
                pinvoke_type<std::meta::return_type_of(Fn), Style>(true),
                s2->name(), pin_params);
        // 3) the public virtual wrapper, branching by origin
        emit_callable_docs<Fn>(w_.members, "        ", cp);
        std::string call_args{w_.handle_field +
                              (cp.wrapper_args.empty()
                                   ? std::string{}
                                   : ", " + cp.wrapper_args) +
                              ", out WelderError _e"};
        code_writer mw{bs.wrapper()};
        mw.line("public virtual {} {}({})",
                public_return_type<std::meta::return_type_of(Fn), Style>(),
                name_, cp.wrapper_params);
        {
            const auto body{mw.braces()};
            mw.line("if (_isDirector)");
            {
                const auto arm{mw.braces()};
                mw.raw(wrapper_return_body<std::meta::return_type_of(Fn),
                                           Style,
                                           ::welder::return_policy_of(
                                               Fn, lang::cs)>(
                    "NativeMethods." + bs_base.name() + "(" + call_args + ")",
                    mw.indentation(), "this"));
            }
            mw.line("else");
            {
                const auto arm{mw.braces()};
                mw.raw(wrapper_return_body<std::meta::return_type_of(Fn),
                                           Style,
                                           ::welder::return_policy_of(
                                               Fn, lang::cs)>(
                    "NativeMethods." + bs.name() + "(" + call_args + ")",
                    mw.indentation(), "this"));
            }
        }
        mw.blank();
    }

    class_writer& w_;  /**< The class being emitted into. */
    std::string name_; /**< The current group's resolved C# name. */
};

} // namespace welder::inline v0::rods::csharp
