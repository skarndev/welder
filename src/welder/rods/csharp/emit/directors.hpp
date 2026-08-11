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
    a C++ virtual.

    One call writes four coordinated things (the model itself is documented in
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

/** Emit the whole director apparatus for welded virtual type @a T: the
    C++ director subclass (into the document's `directors` section), its
    registration/bind thunks, and the managed scaffolding (callbacks, the
    override mask, `_DirBind`). See `directors.hpp` for the model. */
template <class T>
void emit_director(module_writer& m, class_writer& w) {
    static constexpr auto slots{
        std::define_static_array(director_slots(std::meta::dealias(^^T)))};
    const std::string qual{w.cpp_qualified};
    const std::string dir{w.director_ident};
    const std::string idx_base{"wcs::director_slot(^^" + qual + ", "};

    // --- the C++ director subclass -----------------------------------
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
        w.vslots.push_back(class_writer::vslot{sname, vsig, k});
        ++k;
    }
    tbl += "    };\n    static inline wcs_table_t wcs_tbl{};\n";
    d += tbl;
    d += "    ~" + dir + "() override { if (wcs_ctx && wcs_tbl.release) "
         "wcs_tbl.release(wcs_ctx); }\n";
    d += overrides;
    d += "};\n\n";
    m.doc->directors += d;

    // --- registration + bind thunks -----------------------------------
    const std::string init_sym{w.sym_prefix + "_dir_init"};
    const std::string bind_sym{w.sym_prefix + "_dir_bind"};
    m.doc->record_symbol(init_sym);
    m.doc->record_symbol(bind_sym);
    std::string init_params{"void* release"};
    std::string init_body{"    " + dir +
                          "::wcs_tbl.release = "
                          "reinterpret_cast<void (*)(void*)>(release);\n"};
    std::string cs_init_params{"IntPtr release"};
    std::string cs_init_args{
        "                (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, "
        "void>)&_Release"};
    std::string cs_slots{};      // the [UnmanagedCallersOnly] thunks
    std::string cs_mask{};       // the _OverrideMask body lines
    k = 0;
    template for (constexpr auto slot : slots) {
        // Only a slot that is BOUND for cs gets a callback + mask entry: a
        // protected NVI hook or an excluded virtual keeps its table field
        // null, so the director falls through to the qualified base.
        if constexpr (director_slot_supported(slot) &&
                      std::meta::is_public(slot) &&
                      ::welder::member_bound(
                          slot, lang::cs,
                          ::welder::policy_of(
                              std::meta::dealias(^^T)))) {
            const std::string ks{std::to_string(k)};
            std::string wires{};
            std::string cs_wires{};      // C# fnptr generic args (params)
            std::string cs_params{};     // _SlotK parameter list
            std::string conv{};          // arg conversions
            std::string call_args{};
            std::string typeofs{};       // _OverrideMask GetMethod types
            std::size_t j{0};
            template for (constexpr auto p : std::define_static_array(
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
            init_params += ", void* s" + ks;
            init_body += "    " + dir + "::wcs_tbl.s" + ks +
                         " = reinterpret_cast<" +
                         std::string{wire_return_v<
                             std::meta::return_type_of(slot)>} +
                         " (*)(void*" + wires + ", welder_error*)>(s" +
                         ks + ");\n";
            cs_init_params += ", IntPtr s" + ks;
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
            cs_init_args += ",\n                (IntPtr)(delegate* "
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
            cs_slots +=
                "        [UnmanagedCallersOnly(CallConvs = new[] { "
                "typeof(CallConvCdecl) })]\n"
                "        private static unsafe " + cs_ret + " _Slot" + ks +
                "(IntPtr _ctx" + (cs_params.empty() ? "" : ", " + cs_params) +
                ", WelderError* _err)\n        {\n"
                "            try\n            {\n"
                "                var _self = (" + w.cs_name +
                "?)GCHandle.FromIntPtr(_ctx).Target;\n"
                "                if (_self is null) throw new "
                "InvalidOperationException(\"welder: director target "
                "collected\");\n" + conv;
            const std::string mcall{"_self." + mname + "(" + call_args +
                                    ")"};
            if constexpr (rk == marshal_kind::void_) {
                cs_slots += "                " + mcall + ";\n";
            } else if constexpr (rk == marshal_kind::boolean) {
                cs_slots += "                return (byte)(" + mcall +
                            " ? 1 : 0);\n";
            } else if constexpr (rk == marshal_kind::utf8_string) {
                cs_slots += "                return "
                            "NativeMethods.welder_dup_utf8(" + mcall +
                            ");\n";
            } else if constexpr (rk == marshal_kind::handle) {
                if constexpr (is_pointer_flavor(
                                  std::meta::return_type_of(slot))) {
                    // A pointer slot crosses as a VIEW (may be null):
                    // lifetime is the override's contract.
                    cs_slots += "                var _ret = " + mcall +
                                ";\n";
                    cs_slots += "                IntPtr _c = _ret is null "
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
                    cs_slots += "                var _ret = " + mcall +
                                ";\n";
                    cs_slots += "                IntPtr _c = "
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
                cs_slots += "                return " + mcall + ";\n";
            }
            cs_slots +=
                "            }\n            catch (Exception _ex)\n"
                "            {\n"
                "                _err->Code = 7;\n"
                "                _err->Message = "
                "NativeMethods.welder_dup_utf8(_ex.Message);\n" +
                std::string{cs_ret == "void"
                                ? "                return;\n"
                                : "                return default;\n"} +
                "            }\n        }\n";
            cs_mask += "            if (_NotWrapper(_t.GetMethod(\"" +
                       mname + "\", new Type[] { " + typeofs +
                       " })?.DeclaringType)) _m |= 1UL << " + ks + ";\n";
        }
        ++k;
    }
    m.doc->shim += "void " + init_sym + "(" + init_params + ") {\n" +
                   init_body + "}\n\n";
    m.doc->shim += "void " + bind_sym +
                   "(void* self, void* ctx, std::uint64_t mask) {\n"
                   "    if (auto* _d = dynamic_cast<" + dir +
                   "*>(static_cast<" + qual +
                   "*>(self))) { _d->wcs_ctx = ctx; _d->wcs_mask = "
                   "mask; }\n}\n\n";
    m.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial "
                      "void " + init_sym + "(" + cs_init_params + ");\n";
    m.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial "
                      "void " + bind_sym +
                      "(IntPtr self, IntPtr ctx, ulong mask);\n";

    // --- the managed scaffolding --------------------------------------
    std::string anc{};
    static constexpr auto ancestors{std::define_static_array(
        welded_ancestors(std::meta::dealias(^^T)))};
    template for (constexpr auto a : ancestors) {
        anc += " && _d != typeof(" + type_ref<std::meta::dealias(a)>() +
               ")";
    }
    w.members +=
        "        private static bool _cbInit;\n"
        "        private static unsafe void _EnsureCallbacks()\n"
        "        {\n            if (_cbInit) return;\n"
        "            _cbInit = true;\n"
        "            NativeMethods." + init_sym + "(\n" + cs_init_args +
        ");\n        }\n"
        "        [UnmanagedCallersOnly(CallConvs = new[] { "
        "typeof(CallConvCdecl) })]\n"
        "        private static void _Release(IntPtr ctx) => "
        "GCHandle.FromIntPtr(ctx).Free();\n" +
        cs_slots +
        "        private static ulong _OverrideMask(Type _t)\n"
        "        {\n            ulong _m = 0;\n"
        "            if (_t == typeof(" + w.cs_name +
        ")) return _m;\n" + cs_mask +
        "            return _m;\n        }\n"
        "        private static bool _NotWrapper(Type? _d) =>\n"
        "            _d is not null && _d != typeof(" + w.cs_name + ")" +
        anc + ";\n"
        "        private void _DirBind()\n        {\n"
        "            _isDirector = true;\n"
        "            _EnsureCallbacks();\n"
        "            NativeMethods." + bind_sym + "(\n                " +
        w.handle_field + ".DangerousGetHandle(),\n                "
        "GCHandle.ToIntPtr(GCHandle.Alloc(this, GCHandleType.Weak)),\n"
        "                _OverrideMask(GetType()));\n"
        "            GC.KeepAlive(this);\n        }\n\n";
}
} // namespace welder::inline v0::rods::csharp
