#pragma once
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/csharp/directors.hpp>       // eligibility + the slot set
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The **director emitter**: everything a C# subclass needs in order to override
    a C++ virtual. @ref welder::rods::csharp::director_emitter is the component.

    One pass writes four coordinated things (the model itself is documented in
    `<welder/rods/csharp/directors.hpp>`):

    1. the **C++ director subclass**, into the shim's pre-`extern "C"` section —
       its overrides have spliced signatures and a callback-or-qualified-base
       body, guarded by a per-instance override bitmask;
    2. the **registration thunks** `…_dir_init` (install the function-pointer
       table) and `…_dir_bind` (attach a weak `GCHandle` context and the mask);
    3. the managed **`[UnmanagedCallersOnly]` callbacks**, one per overridable
       slot, converting arguments in and the result back out, and trapping any
       managed exception into the error slot as code 7;
    4. the managed **`_OverrideMask`** reflection, which decides — from the
       dynamic C# type — which slots this instance actually overrides.

    The slot's C# method name is not known here (the method sweep names it), so
    the callbacks reference it through a render-time placeholder keyed by
    (declaring class, identifier, signature).
*/

namespace welder::inline v0::rods::csharp {

/** The component that emits the whole director apparatus for one welded
    virtual type: the C++ director subclass, the registration/bind thunks, and
    the managed scaffolding (callbacks, the override mask, `_DirBind`).

    The four steps coordinate through accumulators built per slot — the init
    thunk's parameter list, the managed function-pointer table entries, the
    `[UnmanagedCallersOnly]` callbacks and the `_OverrideMask` lines — which
    live on the object rather than being threaded through one 350-line
    function. One emitter serves one type: construct, call @ref emit, drop. */
class director_emitter {
  public:
    /** Bind the emitter to the type's handles.
        @param m the module handle.
        @param w the class handle (identities already set;
                 `w.is_director`/`w.director_ident` filled by the caller). */
    director_emitter(module_writer& m, class_writer& w)
        : _module{m}, _writer{w} {}

    /** Emit the whole director apparatus for welded virtual type @a T. See
        the file note for the four coordinated pieces; see
        `<welder/rods/csharp/directors.hpp>` for the model.
        @tparam T the director-eligible welded class. */
    template <class T>
    void emit() {
        emit_cpp_subclass<T>();
        // Symbols registered here — before the slot sweep — so a collision
        // diagnoses at the same point the emission is declared.
        const bound_symbol init{*_module.doc, _writer.sym_prefix + "_dir_init",
                                _writer.members, 2};
        const bound_symbol bind{*_module.doc, _writer.sym_prefix + "_dir_bind",
                                _writer.members, 2};
        collect_slots<T>();
        emit_registration_thunks(init, bind);
        emit_managed_scaffolding<T>(init, bind);
    }

  private:
    /** Step 1: the C++ director subclass, into the document's `directors`
        section (before `extern "C"`): the nested function-pointer table, the
        releasing destructor, and one override per slot — spliced signature,
        callback-or-qualified-base body, guarded by the per-instance mask.
        Also records each slot on the class writer so the method sweep can
        match its callables (it has no compile-time handle on the type).
        @tparam T the welded class. */
    template <class T>
    void emit_cpp_subclass() {
        static constexpr auto slots{
            std::define_static_array(director_slots(std::meta::dealias(^^T)))};
        const std::string qual{_writer.cpp_qualified};
        const std::string dir{_writer.director_ident};
        const std::string idx_base{"wcs::director_slot(^^" + qual + ", "};

        // The subclass text interleaves two regions built over the same slot
        // sweep — the nested fnptr table and the overrides — so each grows
        // under its own writer and the assembly stitches them at the end.
        // Plain (uninitialized) members + a value-initialized static: an NSDMI
        // in the nested table would be required before the enclosing class is
        // complete (the static member's {} sits in-class).
        std::string table{};
        code_writer tw{table, 1};
        tw.line("struct wcs_table_t {");
        tw.deeper().line("void (*release)(void*);");
        std::string overrides{};
        code_writer ow{overrides, 1};
        std::size_t k{0};
        template for (constexpr auto slot : slots) {
            const std::string ks{std::to_string(k)};
            const std::string idx{idx_base + ks + ")"};
            static constexpr const char* sname{std::define_static_string(
                std::meta::identifier_of(slot))};
            const std::string name{sname};
            if constexpr (!director_slot_supported(slot)) {
                ow.line("static_assert(false, \"welder: the virtual '{}::{}' "
                        "has a shape the C# director wire cannot carry "
                        "(C-variadic, or a reference/pointer class or string "
                        "return); mark it [[=welder::bind_flat]] to bind it "
                        "non-overridably\");",
                        qual, name);
                ++k;
                continue;
            }
            emit_cpp_override<slot>(tw, ow, qual, ks, idx, name);
            // Record the slot so add_method can match its callables (the
            // method sweep has no compile-time handle on the welded type).
            static constexpr const char* vsig{std::define_static_string(
                std::meta::display_string_of(std::meta::type_of(slot)))};
            _writer.vslots.push_back(class_writer::vslot{sname, vsig, k});
            ++k;
        }
        tw.line("};");
        tw.line("static inline wcs_table_t wcs_tbl{};");

        code_writer d{_module.doc->directors, 0};
        d.line("struct {} final : {} {", dir, qual);
        d.deeper().line("using {}::{};", qual,
                        qual.substr(qual.rfind(':') + 1));
        d.deeper().line("void* wcs_ctx{nullptr};");
        d.deeper().line("std::uint64_t wcs_mask{0};");
        d.raw(table);
        d.deeper().line("~{}() override { if (wcs_ctx && wcs_tbl.release) "
                        "wcs_tbl.release(wcs_ctx); }",
                        dir);
        d.raw(overrides);
        d.line("};");
        d.blank();
    }

    /** One supported slot's C++ side: its function-pointer table entry and
        its override — spliced signature, then the callback-or-qualified-base
        body guarded by the per-instance mask.
        @tparam slot a reflection of the overridable virtual.
        @param tw   the table writer (inside `wcs_table_t`).
        @param ow   the overrides writer (class-body depth).
        @param qual the welded type's `::`-qualified spelling.
        @param ks   the slot index, as text.
        @param idx  the slot's `wcs::director_slot(…)` re-derivation.
        @param name the virtual's identifier. */
    template <std::meta::info slot>
    void emit_cpp_override(code_writer& tw, code_writer& ow,
                           const std::string& qual, const std::string& ks,
                           const std::string& idx, const std::string& name) {
        // table entry: <wire-ret> (*sK)(void*, wires..., welder_error*)
        std::string wires{};
        template for (constexpr auto p : std::define_static_array(
                          std::meta::parameters_of(slot))) {
            wires += ", ";
            wires += wire_param_v<std::meta::type_of(p)>;
        }
        tw.deeper().line("{} (*s{})(void*{}, welder_error*);",
                         wire_return_v<std::meta::return_type_of(slot)>, ks,
                         wires);

        // the override: spliced signature, callback-or-qualified-base body.
        // The signature is assembled first (its parameter list interleaves
        // three growing strings) and passed as ONE argument — cat never scans
        // arguments, so the splice brackets it carries are inert.
        std::string sig{"[: ::std::meta::return_type_of(" + idx + ") :] " +
                        name + "("};
        std::string cargs{};      // the C++ argument names
        std::string wire_args{};  // ctx + converted wire args
        {
            [[maybe_unused]] std::size_t j{0};
            template for ([[maybe_unused]] constexpr auto p :
                          std::define_static_array(
                              std::meta::parameters_of(slot))) {
                const std::string js{std::to_string(j)};
                if (j) {
                    sig += ", ";
                    cargs += ", ";
                }
                const std::string pt{"::std::meta::type_of(::std::meta::"
                                     "parameters_of(" +
                                     idx + ")[" + js + "])"};
                sig += "[: " + pt + " :] a" + js;
                cargs += "a" + js;
                wire_args += ", wcs::shim::to_wire_arg<" + pt + ">(a" + js +
                             ").get()";
                ++j;
            }
        }
        static constexpr const char* quals{
            std::define_static_string(slot_qualifiers(slot))};
        sig += ")" + std::string{quals} + " override {";
        ow.line("{}", sig);
        code_writer body{ow.deeper()};
        body.line("if (wcs_ctx && (wcs_mask & (1ull << {})) && wcs_tbl.s{}) {",
                  ks, ks);
        code_writer call{body.deeper()};
        call.line("welder_error _e{0, nullptr};");
        constexpr bool voidret{classify(std::meta::return_type_of(slot)) ==
                               marshal_kind::void_};
        if constexpr (voidret) {
            call.line("wcs_tbl.s{}(wcs_ctx{}, &_e);", ks, wire_args);
            call.line("if (_e.code != 0) wcs::shim::rethrow_managed(&_e);");
            call.line("return;");
        } else {
            call.line("auto _r = wcs_tbl.s{}(wcs_ctx{}, &_e);", ks, wire_args);
            call.line("if (_e.code != 0) wcs::shim::rethrow_managed(&_e);");
            call.line("return wcs::shim::from_wire_return<"
                      "::std::meta::return_type_of({})>(_r);",
                      idx);
        }
        body.line("}");
        if constexpr (std::meta::is_pure_virtual(slot)) {
            body.line("throw std::runtime_error{\"welder: pure virtual '{}' "
                      "called with no managed override\"};",
                      name);
        } else {
            body.line("return {}::{}({});", qual, name, cargs);
        }
        ow.line("}");
    }

    /** Step 2: the per-slot sweep filling the accumulators — the init thunk's
        parameters and body, the managed init call's function-pointer
        arguments, the `[UnmanagedCallersOnly]` callbacks and the
        `_OverrideMask` lines. Only a slot that is BOUND for cs gets a
        callback + mask entry: a protected NVI hook or an excluded virtual
        keeps its table field null, so the director falls through to the
        qualified base.
        @tparam T the welded class. */
    template <class T>
    void collect_slots() {
        static constexpr auto slots{
            std::define_static_array(director_slots(std::meta::dealias(^^T)))};
        _init_params = "void* release";
        _init_body.clear();
        code_writer{_init_body, 1}.line(
            "{}::wcs_tbl.release = reinterpret_cast<void (*)(void*)>"
            "(release);",
            _writer.director_ident);
        _cs_init_params = "IntPtr release";
        _cs_init_args =
            "                (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, "
            "void>)&_Release";
        std::size_t k{0};
        template for (constexpr auto slot : slots) {
            if constexpr (director_slot_supported(slot) &&
                          std::meta::is_public(slot) &&
                          ::welder::member_bound(
                              slot, lang::cs,
                              ::welder::policy_of(
                                  std::meta::dealias(^^T)))) {
                collect_one_slot<T, slot>(k);
            }
            ++k;
        }
    }

    /** One bound slot's contribution to every accumulator: the wire types,
        the C# callback (argument conversions in, result out, managed
        exceptions → error code 7), and its `_OverrideMask` probe.
        @tparam T    the welded class.
        @tparam slot a reflection of the overridable virtual.
        @param k the slot's index in the type's overridable-slot set. */
    template <class T, std::meta::info slot>
    void collect_one_slot(std::size_t k) {
        const std::string ks{std::to_string(k)};
        std::string wires{};
        std::string cs_wires{};      // C# fnptr generic args (params)
        std::string cs_params{};     // _SlotK parameter list
        std::string conv{};          // arg conversions (depth 4 lines)
        code_writer convw{conv, 4};
        std::string call_args{};
        std::string typeofs{};       // _OverrideMask GetMethod types
        // [[maybe_unused]]: a parameterless slot's instantiation expands the
        // template for zero times, leaving j written but never read.
        [[maybe_unused]] std::size_t j{0};
        template for ([[maybe_unused]] constexpr auto p :
                      std::define_static_array(
                          std::meta::parameters_of(slot))) {
            const std::string js{std::to_string(j)};
            const std::string sep{j ? ", " : ""};
            wires += ", ";
            wires += wire_param_v<std::meta::type_of(p)>;
            constexpr marshal_kind pk{classify(std::meta::type_of(p))};
            std::string cst{};
            if constexpr (pk == marshal_kind::boolean)
                cst = "byte";
            else if constexpr (pk == marshal_kind::scalar) {
                static constexpr const char* csc{
                    scalar_spell(std::meta::type_of(p)).cs};
                cst = csc;
            } else if constexpr (pk == marshal_kind::enum_)
                cst = type_ref<bare(std::meta::type_of(p))>();
            else
                cst = "IntPtr"; // string / handle
            cs_wires += cst + ", ";
            cs_params += sep + cst + " a" + js;
            call_args += sep;
            if constexpr (pk == marshal_kind::boolean) {
                call_args += "a" + js + " != 0";
                typeofs += sep + "typeof(bool)";
            } else if constexpr (pk == marshal_kind::utf8_string) {
                convw.line("string _a{} = Marshal.PtrToStringUTF8(a{}) ?? "
                           "\"\";",
                           js, js);
                call_args += "_a" + js;
                typeofs += sep + "typeof(string)";
            } else if constexpr (pk == marshal_kind::handle) {
                convw.line("var _a{} = new {}(a{}, false);", js,
                           type_ref<bare(std::meta::type_of(p))>(), js);
                call_args += "_a" + js;
                typeofs += sep + "typeof(" +
                           type_ref<bare(std::meta::type_of(p))>() + ")";
            } else {
                call_args += "a" + js;
                typeofs += sep + "typeof(" + cst + ")";
            }
            ++j;
        }
        _init_params += ", void* s" + ks;
        code_writer{_init_body, 1}.line(
            "{}::wcs_tbl.s{} = reinterpret_cast<{} (*)(void*{}, "
            "welder_error*)>(s{});",
            _writer.director_ident, ks,
            wire_return_v<std::meta::return_type_of(slot)>, wires, ks);
        _cs_init_params += ", IntPtr s" + ks;
        constexpr marshal_kind rk{classify(std::meta::return_type_of(slot))};
        std::string cs_ret{};
        if constexpr (rk == marshal_kind::void_) cs_ret = "void";
        else if constexpr (rk == marshal_kind::boolean) cs_ret = "byte";
        else if constexpr (rk == marshal_kind::scalar) {
            static constexpr const char* csr{
                scalar_spell(std::meta::return_type_of(slot)).cs};
            cs_ret = csr;
        } else if constexpr (rk == marshal_kind::enum_)
            cs_ret = type_ref<bare(std::meta::return_type_of(slot))>();
        else cs_ret = "IntPtr";
        _cs_init_args += ",\n                (IntPtr)(delegate* "
                         "unmanaged[Cdecl]<IntPtr, " +
                         cs_wires + "WelderError*, " +
                         (cs_ret == "void" ? "void" : cs_ret) +
                         ">)&_Slot" + ks;
        // the method-name placeholder add_method resolves at render
        static constexpr const char* ssig{std::define_static_string(
            std::meta::display_string_of(std::meta::type_of(slot)))};
        const std::string mname{
            "\x01" +
            std::string{cpp_name_v<std::meta::parent_of(slot)>} + "#" +
            std::string{ident_v<slot>} + "#" + ssig + "\x02"};
        emit_cs_callback<slot>(ks, cs_ret, cs_params, conv,
                               "_self." + mname + "(" + call_args + ")");
        code_writer{_cs_mask_lines, 3}.line(
            "if (_NotWrapper(_t.GetMethod(\"{}\", new Type[] { {} })"
            "?.DeclaringType)) _m |= 1UL << {};",
            mname, typeofs, ks);
    }

    /** One bound slot's `[UnmanagedCallersOnly]` callback: resolve the weak
        context back to the wrapper, convert the arguments in, call the C#
        override (through its render-time name placeholder), convert the
        result out, and trap any managed exception into the error slot as
        code 7.
        @tparam slot a reflection of the overridable virtual.
        @param ks        the slot index, as text.
        @param cs_ret    the callback's C# return spelling.
        @param cs_params the callback's parameter list (after `_ctx`).
        @param conv      the pre-built argument-conversion lines (depth 4).
        @param mcall     the C# override call expression. */
    template <std::meta::info slot>
    void emit_cs_callback(const std::string& ks, const std::string& cs_ret,
                          const std::string& cs_params, const std::string& conv,
                          const std::string& mcall) {
        constexpr marshal_kind rk{classify(std::meta::return_type_of(slot))};
        code_writer cb{_cs_callbacks, 2};
        cb.line("[UnmanagedCallersOnly(CallConvs = new[] { "
                "typeof(CallConvCdecl) })]");
        cb.line("private static unsafe {} _Slot{}(IntPtr _ctx{}, WelderError* "
                "_err)",
                cs_ret, ks, cs_params.empty() ? "" : ", " + cs_params);
        {
            const auto body{cb.braces()};
            cb.line("try");
            {
                const auto arm{cb.braces()};
                cb.line("var _self = ({}?)GCHandle.FromIntPtr(_ctx).Target;",
                        _writer.cs_name);
                cb.line("if (_self is null) throw new "
                        "InvalidOperationException(\"welder: director target "
                        "collected\");");
                cb.raw(conv);
                if constexpr (rk == marshal_kind::void_) {
                    cb.line("{};", mcall);
                } else if constexpr (rk == marshal_kind::boolean) {
                    cb.line("return (byte)({} ? 1 : 0);", mcall);
                } else if constexpr (rk == marshal_kind::utf8_string) {
                    cb.line("return NativeMethods.welder_dup_utf8({});", mcall);
                } else if constexpr (rk == marshal_kind::handle) {
                    if constexpr (is_pointer_flavor(
                                      std::meta::return_type_of(slot))) {
                        // A pointer slot crosses as a VIEW (may be null):
                        // lifetime is the override's contract.
                        cb.line("var _ret = {};", mcall);
                        cb.line("IntPtr _c = _ret is null ? IntPtr.Zero : "
                                "_ret._h_{}.DangerousGetHandle();",
                                field_ref<bare(
                                    std::meta::return_type_of(slot))>());
                        cb.line("GC.KeepAlive(_ret);");
                        cb.line("return _c;");
                    } else {
                        // Clone through the return class's copy thunk so the
                        // copy exists before the managed temporary can be
                        // collected.
                        cb.line("var _ret = {};", mcall);
                        cb.line("IntPtr _c = NativeMethods.welder_{}_clone("
                                "_ret._h_{}, out WelderError _e2);",
                                std::string_view{upath_v<bare(
                                    std::meta::return_type_of(slot))>},
                                field_ref<bare(
                                    std::meta::return_type_of(slot))>());
                        cb.line("WelderInterop.ThrowIfError(in _e2);");
                        cb.line("GC.KeepAlive(_ret);");
                        cb.line("return _c;");
                    }
                } else {
                    cb.line("return {};", mcall);
                }
            }
            cb.line("catch (Exception _ex)");
            {
                const auto arm{cb.braces()};
                cb.line("_err->Code = 7;");
                cb.line("_err->Message = "
                        "NativeMethods.welder_dup_utf8(_ex.Message);");
                cb.line(cs_ret == "void" ? "return;" : "return default;");
            }
        }
    }

    /** Step 3: the `…_dir_init` (install the table) and `…_dir_bind` (attach
        the weak-`GCHandle` context + mask) thunks and their P/Invokes.
        @param init the init thunk's symbol and sinks.
        @param bind the bind thunk's symbol and sinks. */
    void emit_registration_thunks(const bound_symbol& init,
                                  const bound_symbol& bind) {
        code_writer t{init.thunk()};
        t.line("void {}({}) {", init.name(), _init_params);
        t.raw(_init_body);
        t.line("}");
        t.blank();
        t.line("void {}(void* self, void* ctx, std::uint64_t mask) {",
               bind.name());
        t.deeper().line(
            "if (auto* _d = dynamic_cast<{}*>(static_cast<{}*>(self))) { "
            "_d->wcs_ctx = ctx; _d->wcs_mask = mask; }",
            _writer.director_ident, _writer.cpp_qualified);
        t.line("}");
        t.blank();
        init.pinvoke().line(
            "[LibraryImport(Lib)] internal static partial void {}({});",
            init.name(), _cs_init_params);
        bind.pinvoke().line(
            "[LibraryImport(Lib)] internal static partial void {}(IntPtr "
            "self, IntPtr ctx, ulong mask);",
            bind.name());
    }

    /** Step 4: the managed scaffolding — `_EnsureCallbacks` (one-time table
        install), `_Release`, the accumulated `_Slot<k>` callbacks,
        `_OverrideMask` (with the welded-ancestor exclusion, so an inherited
        wrapper method never counts as an override) and `_DirBind`.
        @tparam T the welded class.
        @param init the init thunk's symbol.
        @param bind the bind thunk's symbol. */
    template <class T>
    void emit_managed_scaffolding(const bound_symbol& init,
                                  const bound_symbol& bind) {
        std::string anc{};
        static constexpr auto ancestors{std::define_static_array(
            welded_ancestors(std::meta::dealias(^^T)))};
        template for (constexpr auto a : ancestors) {
            anc += " && _d != typeof(" + type_ref<std::meta::dealias(a)>() +
                   ")";
        }
        code_writer mw{init.wrapper()};
        mw.line("private static bool _cbInit;");
        mw.line("private static unsafe void _EnsureCallbacks()");
        {
            const auto body{mw.braces()};
            mw.line("if (_cbInit) return;");
            mw.line("_cbInit = true;");
            // A multi-line call expression (one fnptr argument per line) —
            // written raw, since it manages its own continuation layout.
            mw.raw(mw.indentation() + "NativeMethods." + init.name() + "(\n" +
                   _cs_init_args + ");\n");
        }
        mw.line("[UnmanagedCallersOnly(CallConvs = new[] { "
                "typeof(CallConvCdecl) })]");
        mw.line("private static void _Release(IntPtr ctx) => "
                "GCHandle.FromIntPtr(ctx).Free();");
        mw.raw(_cs_callbacks);
        mw.line("private static ulong _OverrideMask(Type _t)");
        {
            const auto body{mw.braces()};
            mw.line("ulong _m = 0;");
            mw.line("if (_t == typeof({})) return _m;", _writer.cs_name);
            mw.raw(_cs_mask_lines);
            mw.line("return _m;");
        }
        mw.line("private static bool _NotWrapper(Type? _d) =>");
        mw.deeper().line("_d is not null && _d != typeof({}){};",
                         _writer.cs_name, anc);
        mw.line("private void _DirBind()");
        {
            const auto body{mw.braces()};
            mw.line("_isDirector = true;");
            mw.line("_EnsureCallbacks();");
            // Another deliberate raw: the bind call spreads its three
            // arguments across continuation lines.
            mw.raw(mw.indentation() + "NativeMethods." + bind.name() +
                   "(\n                " + _writer.handle_field +
                   ".DangerousGetHandle(),\n                "
                   "GCHandle.ToIntPtr(GCHandle.Alloc(this, "
                   "GCHandleType.Weak)),\n"
                   "                _OverrideMask(GetType()));\n");
            mw.line("GC.KeepAlive(this);");
        }
        mw.blank();
    }

    module_writer& _module;      /**< The module handle. */
    class_writer& _writer;       /**< The class being emitted into. */
    std::string _init_params;    /**< The init thunk's C parameter list. */
    std::string _init_body;      /**< The init thunk's table-install lines. */
    std::string _cs_init_params; /**< The init P/Invoke's parameter list. */
    std::string _cs_init_args;   /**< The managed init call's fnptr args. */
    std::string _cs_callbacks;   /**< The `[UnmanagedCallersOnly]` callbacks. */
    std::string _cs_mask_lines;  /**< The `_OverrideMask` probe lines. */
};

/** Emit the whole director apparatus for welded virtual type @a T — the
    entry @ref class_opener calls (see @ref director_emitter).
    @tparam T the director-eligible welded class.
    @param m the module handle.
    @param w the class handle. */
template <class T>
void emit_director(module_writer& m, class_writer& w) {
    director_emitter{m, w}.emit<T>();
}

} // namespace welder::inline v0::rods::csharp
