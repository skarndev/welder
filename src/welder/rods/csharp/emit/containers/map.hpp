#pragma once
#include <cctype>
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The generated wrapper for a **`std::map` / `std::unordered_map` with a leaf
    key**: `Count`, `ContainsKey`, a `this[K]` indexer (insert-or-assign on set),
    `Remove` and `Clear`.
    @ref welder::rods::csharp::map_wrapper_emitter is the component.

    A welded mapped type reads as a **live view** pinned to the map wrapper, on
    the same rationale as the sequence wrappers; a leaf mapped type crosses by
    value. Only the default-argument map form is admitted — a custom comparator
    or hasher makes the re-derived spelling a different type, so those are a
    designed `classify` rejection rather than a silently wrong cast.
*/

namespace welder::inline v0::rods::csharp {

/** The component that generates the reference-semantic map wrapper
    (`std::map`/`std::unordered_map` with a leaf key): Count, ContainsKey, a
    `this[K]` indexer (a live view for a welded mapped type, a value copy
    otherwise; insert-or-assign on set), Remove, Clear.

    @ref ensure derives the key/value spellings in every register they cross
    in (wire, P/Invoke, public C#) — the inbound conversions reuse
    @ref append_one_param, the SAME conversion source as params/setters — and
    then writes the three artifacts as named steps. */
class map_wrapper_emitter {
  public:
    /** Bind the emitter to the growing document.
        @param doc the two-artifact document. */
    explicit map_wrapper_emitter(document& doc) : doc_{&doc} {}

    /** Generate the wrapper for map specialization @a C, if this is the first
        time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the map specialization (default form only —
                  leaf key, no custom comparator/hasher). */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!doc_->claim_container(key))
            return;
        constexpr bool ordered{is_specialization_of(C, ^^std::map)};
        ordered_ = ordered;
        static constexpr const char* ktok{
            std::define_static_string(map_token(map_key_type(C)))};
        static constexpr const char* vtok{
            std::define_static_string(map_token(map_value_type(C)))};
        sym_ = std::string{ordered ? "welder_map_" : "welder_umap_"} + ktok +
               "_" + vtok;
        static constexpr const char* kcpp{std::define_static_string(
            leaf_cpp_spelling(map_key_type(C)))};
        static constexpr const char* vcpp{std::define_static_string(
            leaf_cpp_spelling(map_value_type(C)))};
        // A welded key/value defers to the anchor registry (it may be an
        // alias-welded specialization); scalars and strings spell themselves.
        const std::string kanch{
            classify(map_key_type(C)) == marshal_kind::handle ||
                    classify(map_key_type(C)) == marshal_kind::enum_
                ? anchor_ref<bare(map_key_type(C))>()
                : std::string{kcpp}};
        const std::string vanch{
            classify(map_value_type(C)) == marshal_kind::handle ||
                    classify(map_value_type(C)) == marshal_kind::enum_
                ? anchor_ref<bare(map_value_type(C))>()
                : std::string{vcpp}};
        targs_ = std::string{ordered ? "true" : "false"} + ", ^^" + kanch +
                 ", ^^" + vanch;
        V_ = container_ref<C>();
        // Wrapper name: Map/UMap + CapKey + value name (identifier-safe).
        std::string kname{ktok};
        kname[0] = static_cast<char>(std::toupper(kname[0]));
        std::string vname{};
        if constexpr (classify(map_value_type(C)) == marshal_kind::handle ||
                      classify(map_value_type(C)) == marshal_kind::enum_)
            vname = field_ref<bare(map_value_type(C))>();
        else {
            std::string t{vtok};
            t[0] = static_cast<char>(std::toupper(t[0]));
            vname = t;
        }
        doc_->record_type_name(key, std::string{ordered ? "Map" : "UMap"} +
                                        kname + vname);

        // key/value piece reuse: the SAME conversion source as params/setters
        append_one_param<map_key_type(C), ::welder::naming::none>(kcp_, 0,
                                                                  "key");
        append_one_param<map_value_type(C), ::welder::naming::none>(vcp_, 1,
                                                                    "value");
        kwire_ = wire_param_v<map_key_type(C)>;
        vwire_ = wire_param_v<map_value_type(C)>;
        kpin_ = pinvoke_type<map_key_type(C), ::welder::naming::none>(false);
        vpin_ = pinvoke_type<map_value_type(C), ::welder::naming::none>(false);
        constexpr bool v_is_handle{classify(map_value_type(C)) ==
                                   marshal_kind::handle};
        vget_ret_ = v_is_handle ? std::string{"IntPtr"}
                                : pinvoke_type<map_value_type(C),
                                               ::welder::naming::none>(true);
        vget_wire_ = v_is_handle
                         ? std::string{"void*"}
                         : std::string{wire_return_v<map_value_type(C)>};
        for (const char* leaf : {"_new", "_destroy", "_size", "_contains",
                                 "_get", "_set", "_remove", "_clear"})
            doc_->record_symbol(sym_ + leaf);
        kattr_ = import_attr(kcp_.has_string);
        kvattr_ = import_attr(kcp_.has_string || vcp_.has_string);
        kpub_ = public_type<map_key_type(C), ::welder::naming::none>();
        vpub_ = public_type<map_value_type(C), ::welder::naming::none>();
        get_body_ = wrapper_return_body<map_value_type(C),
                                        ::welder::naming::none,
                                        field_return_policy(
                                            map_value_type(C))>(
            "NativeMethods." + sym_ + "_get(_h_" + V_ + ", " +
                kcp_.wrapper_args + ", out WelderError _e)",
            "                ", "this");
        emit_thunks();
        emit_pinvokes();
        emit_wrapper();
    }

  private:
    /** Write the native op thunks — one-line delegations into the compiled
        `shim::map_*` support templates, parameterized by orderedness, key
        anchor and value anchor. */
    void emit_thunks() {
        code_writer t{doc_->shim, 0};
        t.line("void* {}_new(welder_error* err) { return "
               "wcs::shim::map_new<{}>(err); }",
               sym_, targs_);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::map_destroy<{}>"
               "(self); }",
               sym_, targs_);
        t.blank();
        t.line("std::int64_t {}_size(void* self, welder_error* err) { "
               "return wcs::shim::map_size<{}>(self, err); }",
               sym_, targs_);
        t.blank();
        t.line("bool {}_contains(void* self, {} k, welder_error* err) { "
               "return wcs::shim::map_contains<{}>(self, k, err); }",
               sym_, kwire_, targs_);
        t.blank();
        t.line("{} {}_get(void* self, {} k, welder_error* err) { return "
               "wcs::shim::map_get<{}>(self, k, err); }",
               vget_wire_, sym_, kwire_, targs_);
        t.blank();
        t.line("void {}_set(void* self, {} k, {} v, welder_error* err) { "
               "wcs::shim::map_set<{}>(self, k, v, err); }",
               sym_, kwire_, vwire_, targs_);
        t.blank();
        t.line("bool {}_remove(void* self, {} k, welder_error* err) { "
               "return wcs::shim::map_remove<{}>(self, k, err); }",
               sym_, kwire_, targs_);
        t.blank();
        t.line("void {}_clear(void* self, welder_error* err) { "
               "wcs::shim::map_clear<{}>(self, err); }",
               sym_, targs_);
        t.blank();
    }

    /** Write the `[LibraryImport]` declarations for the op thunks (UTF-8
        marshalling attributes when a string key/value crosses; `bool` results
        as `U1`). */
    void emit_pinvokes() {
        code_writer p{doc_->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_new(out WelderError err);",
               sym_);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_destroy(IntPtr self);",
               sym_);
        p.line("[LibraryImport(Lib)] internal static partial long {}"
               "_size({}Handle self, out WelderError err);",
               sym_, V_);
        p.line("{} [return: MarshalAs(UnmanagedType.U1)] internal static "
               "partial bool {}_contains({}Handle self, {} k, out "
               "WelderError err);",
               kattr_, sym_, V_, kpin_);
        p.line("{} internal static partial {} {}_get({}Handle self, {} k, "
               "out WelderError err);",
               kattr_, vget_ret_, sym_, V_, kpin_);
        p.line("{} internal static partial void {}_set({}Handle self, {} "
               "k, {} v, out WelderError err);",
               kvattr_, sym_, V_, kpin_, vpin_);
        p.line("{} [return: MarshalAs(UnmanagedType.U1)] internal static "
               "partial bool {}_remove({}Handle self, {} k, out "
               "WelderError err);",
               kattr_, sym_, V_, kpin_);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_clear({}Handle self, out WelderError err);",
               sym_, V_);
    }

    /** Write the managed side: the `SafeHandle` owning the native map and the
        public wrapper class — `Count`, `ContainsKey`, the `this[K]` indexer
        (insert-or-assign on set), `Remove`, `Clear`, `Dispose`. */
    void emit_wrapper() {
        code_writer w{doc_->containers, 1};
        w.line("internal sealed class {}Handle : SafeHandle", V_);
        {
            const auto cls{w.braces()};
            w.line("internal {}Handle(IntPtr handle, bool owns) : "
                   "base(IntPtr.Zero, owns)",
                   V_);
            {
                const auto body{w.braces()};
                w.line("SetHandle(handle);");
            }
            w.line("public override bool IsInvalid => handle == IntPtr.Zero;");
            w.line("protected override bool ReleaseHandle()");
            {
                const auto body{w.braces()};
                w.line("NativeMethods.{}_destroy(handle);", sym_);
                w.line("return true;");
            }
        }
        w.blank();
        w.line("/// <summary>A reference-semantic C++ {} of {} to "
               "{}.</summary>",
               ordered_ ? "std::map" : "std::unordered_map", kpub_, vpub_);
        w.line("public sealed class {} : IDisposable", V_);
        {
            const auto cls{w.braces()};
            w.line("internal {}Handle _h_{};", V_, V_);
            w.line("internal object? _owner;");
            w.line("internal {}(IntPtr handle, bool owns) { _h_{} = new "
                   "{}Handle(handle, owns); }",
                   V_, V_, V_);
            // An empty C# body is a literal "{}" — an argument, never format
            // text (cat would eat it as a placeholder).
            w.line("public {}() : this(_New(), true) {}", V_, "{}");
            w.line("private static IntPtr _New()");
            {
                const auto body{w.braces()};
                w.line("IntPtr _r = NativeMethods.{}_new(out WelderError _e);",
                       sym_);
                w.line("WelderInterop.ThrowIfError(in _e);");
                w.line("return _r;");
            }
            w.line("public int Count");
            {
                const auto prop{w.braces()};
                w.line("get");
                {
                    const auto arm{w.braces()};
                    w.line("var _r = NativeMethods.{}_size(_h_{}, out "
                           "WelderError _e);",
                           sym_, V_);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                    w.line("return (int)_r;");
                }
            }
            w.line("public bool ContainsKey({} key)", kpub_);
            {
                const auto body{w.braces()};
                w.line("var _r = NativeMethods.{}_contains(_h_{}, {}, out "
                       "WelderError _e);",
                       sym_, V_, kcp_.wrapper_args);
                w.line("WelderInterop.ThrowIfError(in _e);");
                w.line("return _r;");
            }
            w.line("public {} this[{} key]", vpub_, kpub_);
            {
                const auto prop{w.braces()};
                w.line("get");
                {
                    const auto arm{w.braces()};
                    w.raw(get_body_);
                }
                w.line("set");
                {
                    const auto arm{w.braces()};
                    w.line("NativeMethods.{}_set(_h_{}, {}, out "
                           "WelderError _e);",
                           sym_, V_, kcp_.wrapper_args + vcp_.wrapper_args);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                }
            }
            w.line("public bool Remove({} key)", kpub_);
            {
                const auto body{w.braces()};
                w.line("var _r = NativeMethods.{}_remove(_h_{}, {}, out "
                       "WelderError _e);",
                       sym_, V_, kcp_.wrapper_args);
                w.line("WelderInterop.ThrowIfError(in _e);");
                w.line("return _r;");
            }
            w.line("public void Clear()");
            {
                const auto body{w.braces()};
                w.line("NativeMethods.{}_clear(_h_{}, out WelderError _e);",
                       sym_, V_);
                w.line("WelderInterop.ThrowIfError(in _e);");
            }
            w.line("public void Dispose() => _h_{}.Dispose();", V_);
        }
        w.blank();
    }

    document* doc_;         /**< The shared document. */
    bool ordered_{false};   /**< `std::map` vs `std::unordered_map`. */
    std::string sym_{};     /**< The C symbol stem (`welder_[u]map_<k>_<v>`). */
    std::string targs_{};   /**< The shim's template arguments
                                 (`ordered, ^^key, ^^value`). */
    std::string V_{};       /**< The wrapper class's name reference. */
    std::string kwire_{};   /**< The key's C-ABI parameter spelling. */
    std::string vwire_{};   /**< The value's C-ABI parameter spelling. */
    std::string kpin_{};    /**< The key's P/Invoke parameter type. */
    std::string vpin_{};    /**< The value's P/Invoke parameter type. */
    std::string vget_ret_{};  /**< `_get`'s P/Invoke return type (a handle
                                   value reads as `IntPtr` — a live view). */
    std::string vget_wire_{}; /**< `_get`'s C-ABI return spelling. */
    std::string kattr_{};   /**< The key-only `[LibraryImport]` attribute. */
    std::string kvattr_{};  /**< The key+value `[LibraryImport]` attribute. */
    std::string kpub_{};    /**< The key's public C# type. */
    std::string vpub_{};    /**< The value's public C# type. */
    std::string get_body_{}; /**< The indexer getter's pre-indented body. */
    call_pieces kcp_{};     /**< The key's conversion pieces. */
    call_pieces vcp_{};     /**< The value's conversion pieces. */
};

/** The reference-semantic map wrapper (`std::map`/`std::unordered_map`
    with a leaf key): Count, ContainsKey, a `this[K]` indexer (a live view
    for a welded mapped type, a value copy otherwise; insert-or-assign on
    set), Remove, Clear. Forwards into
    @ref welder::rods::csharp::map_wrapper_emitter.
    @tparam C a reflection of the map specialization.
    @param doc the growing document. */
template <std::meta::info C>
void ensure_map(document& doc) {
    map_wrapper_emitter{doc}.ensure<C>();
}

} // namespace welder::inline v0::rods::csharp
