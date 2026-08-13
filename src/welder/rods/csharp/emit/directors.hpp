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
    director_emitter(module_writer& m, class_writer& w) : m_{m}, w_{w} {}

    /** Emit the whole director apparatus for welded virtual type @a T. See
        the file note for the four coordinated pieces; see
        `<welder/rods/csharp/directors.hpp>` for the model.
        @tparam T the director-eligible welded class. */
    template <class T>
    void emit() {
        emit_cpp_subclass<T>();
        // Symbols registered here — before the slot sweep — so a collision
        // diagnoses at the same point the emission is declared.
        const bound_symbol init{*m_.doc, w_.sym_prefix + "_dir_init",
                                w_.members, 2};
        const bound_symbol bind{*m_.doc, w_.sym_prefix + "_dir_bind",
                                w_.members, 2};
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
        const std::string qual{w_.cpp_qualified};
        const std::string dir{w_.director_ident};
        const std::string idx_base{"wcs::director_slot(^^" + qual + ", "};

        std::string d{};
        // Plain (uninitialized) members + a value-initialized static: an NSDMI
        // in the nested table would be required before the enclosing class is
        // complete (the static member's {} sits in-class).
        std::string tbl{"    struct wcs_table_t {\n"
                        "        void (*release)(void*);\n"};
        d += "struct " + dir + " final : " + qual + " {\n";
        d += "    using " + qual + "::" + qual.substr(qual.rfind(':') + 1) +
             ";\n";
        d += "    void* wcs_ctx{nullptr};\n";
        d += "    std::uint64_t wcs_mask{0};\n";
        std::string overrides{};
        std::size_t k{0};
        template for (constexpr auto slot : slots) {
            const std::string ks{std::to_string(k)};
            const std::string idx{idx_base + ks + ")"};
            static constexpr const char* sname{std::define_static_string(
                std::meta::identifier_of(slot))};
            const std::string name{sname};
            if constexpr (!director_slot_supported(slot)) {
                overrides += "    static_assert(false, \"welder: the virtual '" +
                             qual + "::" + name +
                             "' has a shape the C# director wire cannot carry "
                             "(C-variadic, or a reference/pointer class or "
                             "string return); mark it "
                             "[[=welder::bind_flat]] to bind it "
                             "non-overridably\");\n";
                ++k;
                continue;
            }
            // table entry: <wire-ret> (*sK)(void*, wires..., welder_error*)
            std::string wires{};
            template for (constexpr auto p : std::define_static_array(
                              std::meta::parameters_of(slot))) {
                wires += ", ";
                wires += wire_param_v<std::meta::type_of(p)>;
            }
            tbl += "        " +
                   std::string{
                       wire_return_v<std::meta::return_type_of(slot)>} +
                   " (*s" + ks + ")(void*" + wires +
                   ", welder_error*);\n";

            // the override: spliced signature, callback-or-qualified-base body
            overrides += "    [: ::std::meta::return_type_of(" + idx + ") :] " +
                         name + "(";
            std::string cargs{};      // the C++ argument names
            std::string wire_args{};  // ctx + converted wire args
            {
                [[maybe_unused]] std::size_t j{0};
                template for ([[maybe_unused]] constexpr auto p :
                              std::define_static_array(
                                  std::meta::parameters_of(slot))) {
                    const std::string js{std::to_string(j)};
                    if (j) {
                        overrides += ", ";
                        cargs += ", ";
                    }
                    const std::string pt{"::std::meta::type_of(::std::meta::"
                                         "parameters_of(" +
                                         idx + ")[" + js + "])"};
                    overrides += "[: " + pt + " :] a" + js;
                    cargs += "a" + js;
                    wire_args += ", wcs::shim::to_wire_arg<" + pt + ">(a" + js +
                                 ").get()";
                    ++j;
                }
            }
            static constexpr const char* quals{
                std::define_static_string(slot_qualifiers(slot))};
            overrides += ")" + std::string{quals} + " override {\n";
            overrides += "        if (wcs_ctx && (wcs_mask & (1ull << " +
                         ks + ")) && wcs_tbl.s" + ks + ") {\n";
            overrides += "            welder_error _e{0, nullptr};\n";
            constexpr bool voidret{
                classify(std::meta::return_type_of(slot)) ==
                marshal_kind::void_};
            if constexpr (voidret) {
                overrides += "            wcs_tbl.s" + ks + "(wcs_ctx" +
                             wire_args + ", &_e);\n";
                overrides += "            if (_e.code != 0) "
                             "wcs::shim::rethrow_managed(&_e);\n";
                overrides += "            return;\n";
            } else {
                overrides += "            auto _r = wcs_tbl.s" + ks +
                             "(wcs_ctx" + wire_args + ", &_e);\n";
                overrides += "            if (_e.code != 0) "
                             "wcs::shim::rethrow_managed(&_e);\n";
                overrides += "            return wcs::shim::from_wire_return<"
                             "::std::meta::return_type_of(" +
                             idx + ")>(_r);\n";
            }
            overrides += "        }\n";
            if constexpr (std::meta::is_pure_virtual(slot)) {
                overrides += "        throw std::runtime_error{\"welder: pure "
                             "virtual '" + name +
                             "' called with no managed override\"};\n";
            } else {
                overrides += "        return " + qual + "::" + name + "(" +
                             cargs + ");\n";
            }
            overrides += "    }\n";

            // Record the slot so add_method can match its callables (the
            // method sweep has no compile-time handle on the welded type).
            static constexpr const char* vsig{std::define_static_string(
                std::meta::display_string_of(std::meta::type_of(slot)))};
            w_.vslots.push_back(class_writer::vslot{sname, vsig, k});
            ++k;
        }
        tbl += "    };\n    static inline wcs_table_t wcs_tbl{};\n";
        d += tbl;
        d += "    ~" + dir + "() override { if (wcs_ctx && wcs_tbl.release) "
             "wcs_tbl.release(wcs_ctx); }\n";
        d += overrides;
        d += "};\n\n";
        m_.doc->directors += d;
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
        const std::string dir{w_.director_ident};
        init_params_ = "void* release";
        init_body_ = "    " + dir +
                     "::wcs_tbl.release = "
                     "reinterpret_cast<void (*)(void*)>(release);\n";
        cs_init_params_ = "IntPtr release";
        cs_init_args_ =
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
        const std::string dir{w_.director_ident};
        std::string wires{};
        std::string cs_wires{};      // C# fnptr generic args (params)
        std::string cs_params{};     // _SlotK parameter list
        std::string conv{};          // arg conversions
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
                conv += "                string _a" + js +
                        " = Marshal.PtrToStringUTF8(a" + js +
                        ") ?? \"\";\n";
                call_args += "_a" + js;
                typeofs += sep + "typeof(string)";
            } else if constexpr (pk == marshal_kind::handle) {
                conv += "                var _a" + js + " = new " +
                        type_ref<bare(std::meta::type_of(p))>() +
                        "(a" + js + ", false);\n";
                call_args += "_a" + js;
                typeofs += sep + "typeof(" +
                           type_ref<bare(std::meta::type_of(p))>() +
                           ")";
            } else {
                call_args += "a" + js;
                typeofs += sep + "typeof(" + cst + ")";
            }
            ++j;
        }
        init_params_ += ", void* s" + ks;
        init_body_ += "    " + dir + "::wcs_tbl.s" + ks +
                      " = reinterpret_cast<" +
                      std::string{wire_return_v<
                          std::meta::return_type_of(slot)>} +
                      " (*)(void*" + wires + ", welder_error*)>(s" +
                      ks + ");\n";
        cs_init_params_ += ", IntPtr s" + ks;
        constexpr marshal_kind rk{
            classify(std::meta::return_type_of(slot))};
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
        cs_init_args_ += ",\n                (IntPtr)(delegate* "
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
        cs_slots_ +=
            "        [UnmanagedCallersOnly(CallConvs = new[] { "
            "typeof(CallConvCdecl) })]\n"
            "        private static unsafe " + cs_ret + " _Slot" + ks +
            "(IntPtr _ctx" + (cs_params.empty() ? "" : ", " + cs_params) +
            ", WelderError* _err)\n        {\n"
            "            try\n            {\n"
            "                var _self = (" + w_.cs_name +
            "?)GCHandle.FromIntPtr(_ctx).Target;\n"
            "                if (_self is null) throw new "
            "InvalidOperationException(\"welder: director target "
            "collected\");\n" + conv;
        const std::string mcall{"_self." + mname + "(" + call_args +
                                ")"};
        if constexpr (rk == marshal_kind::void_) {
            cs_slots_ += "                " + mcall + ";\n";
        } else if constexpr (rk == marshal_kind::boolean) {
            cs_slots_ += "                return (byte)(" + mcall +
                         " ? 1 : 0);\n";
        } else if constexpr (rk == marshal_kind::utf8_string) {
            cs_slots_ += "                return "
                         "NativeMethods.welder_dup_utf8(" + mcall +
                         ");\n";
        } else if constexpr (rk == marshal_kind::handle) {
            if constexpr (is_pointer_flavor(
                              std::meta::return_type_of(slot))) {
                // A pointer slot crosses as a VIEW (may be null):
                // lifetime is the override's contract.
                cs_slots_ += "                var _ret = " + mcall +
                             ";\n";
                cs_slots_ += "                IntPtr _c = _ret is null "
                             "? IntPtr.Zero : _ret._h_" +
                             field_ref<bare(
                                 std::meta::return_type_of(slot))>() +
                             ".DangerousGetHandle();\n"
                             "                GC.KeepAlive(_ret);\n"
                             "                return _c;\n";
            } else {
                // Clone through the return class's copy thunk so the
                // copy exists before the managed temporary can be
                // collected.
                cs_slots_ += "                var _ret = " + mcall +
                             ";\n";
                cs_slots_ += "                IntPtr _c = "
                             "NativeMethods.welder_" +
                             std::string{upath_v<bare(
                                 std::meta::return_type_of(slot))>} +
                             "_clone(_ret._h_" +
                             field_ref<bare(
                                 std::meta::return_type_of(slot))>() +
                             ", out WelderError _e2);\n"
                             "                "
                             "WelderInterop.ThrowIfError(in _e2);\n"
                             "                GC.KeepAlive(_ret);\n"
                             "                return _c;\n";
            }
        } else {
            cs_slots_ += "                return " + mcall + ";\n";
        }
        cs_slots_ +=
            "            }\n            catch (Exception _ex)\n"
            "            {\n"
            "                _err->Code = 7;\n"
            "                _err->Message = "
            "NativeMethods.welder_dup_utf8(_ex.Message);\n" +
            std::string{cs_ret == "void"
                            ? "                return;\n"
                            : "                return default;\n"} +
            "            }\n        }\n";
        cs_mask_ += "            if (_NotWrapper(_t.GetMethod(\"" +
                    mname + "\", new Type[] { " + typeofs +
                    " })?.DeclaringType)) _m |= 1UL << " + ks + ";\n";
    }

    /** Step 3: the `…_dir_init` (install the table) and `…_dir_bind` (attach
        the weak-`GCHandle` context + mask) thunks and their P/Invokes.
        @param init the init thunk's symbol and sinks.
        @param bind the bind thunk's symbol and sinks. */
    void emit_registration_thunks(const bound_symbol& init,
                                  const bound_symbol& bind) {
        code_writer t{init.thunk()};
        t.line("void {}({}) {", init.name(), init_params_);
        t.raw(init_body_);
        t.line("}");
        t.blank();
        t.line("void {}(void* self, void* ctx, std::uint64_t mask) {",
               bind.name());
        t.deeper().line(
            "if (auto* _d = dynamic_cast<{}*>(static_cast<{}*>(self))) { "
            "_d->wcs_ctx = ctx; _d->wcs_mask = mask; }",
            w_.director_ident, w_.cpp_qualified);
        t.line("}");
        t.blank();
        init.pinvoke().line(
            "[LibraryImport(Lib)] internal static partial void {}({});",
            init.name(), cs_init_params_);
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
            mw.raw(mw.indentation() + "NativeMethods." + init.name() + "(\n" +
                   cs_init_args_ + ");\n");
        }
        mw.line("[UnmanagedCallersOnly(CallConvs = new[] { "
                "typeof(CallConvCdecl) })]");
        mw.line("private static void _Release(IntPtr ctx) => "
                "GCHandle.FromIntPtr(ctx).Free();");
        mw.raw(cs_slots_);
        mw.line("private static ulong _OverrideMask(Type _t)");
        {
            const auto body{mw.braces()};
            mw.line("ulong _m = 0;");
            mw.line("if (_t == typeof({})) return _m;", w_.cs_name);
            mw.raw(cs_mask_);
            mw.line("return _m;");
        }
        mw.line("private static bool _NotWrapper(Type? _d) =>");
        mw.deeper().line("_d is not null && _d != typeof({}){};", w_.cs_name,
                         anc);
        mw.line("private void _DirBind()");
        {
            const auto body{mw.braces()};
            mw.line("_isDirector = true;");
            mw.line("_EnsureCallbacks();");
            mw.raw(mw.indentation() + "NativeMethods." + bind.name() +
                   "(\n                " + w_.handle_field +
                   ".DangerousGetHandle(),\n                "
                   "GCHandle.ToIntPtr(GCHandle.Alloc(this, "
                   "GCHandleType.Weak)),\n"
                   "                _OverrideMask(GetType()));\n");
            mw.line("GC.KeepAlive(this);");
        }
        mw.blank();
    }

    module_writer& m_;           /**< The module handle. */
    class_writer& w_;            /**< The class being emitted into. */
    std::string init_params_;    /**< The init thunk's C parameter list. */
    std::string init_body_;      /**< The init thunk's table-install lines. */
    std::string cs_init_params_; /**< The init P/Invoke's parameter list. */
    std::string cs_init_args_;   /**< The managed init call's fnptr args. */
    std::string cs_slots_;       /**< The `[UnmanagedCallersOnly]` callbacks. */
    std::string cs_mask_;        /**< The `_OverrideMask` probe lines. */
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
