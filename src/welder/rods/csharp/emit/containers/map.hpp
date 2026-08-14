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
    explicit map_wrapper_emitter(document& doc) : _doc{&doc} {}

    /** Generate the wrapper for map specialization @a C, if this is the first
        time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the map specialization (default form only —
                  leaf key, no custom comparator/hasher). */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!_doc->claim_container(key))
            return;
        constexpr bool ordered{is_specialization_of(C, ^^std::map)};
        _ordered = ordered;
        static constexpr const char* ktok{
            std::define_static_string(map_token(map_key_type(C)))};
        static constexpr const char* vtok{
            std::define_static_string(map_token(map_value_type(C)))};
        _symbol_stem = std::string{ordered ? "welder_map_" : "welder_umap_"} + ktok +
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
        _template_args = std::string{ordered ? "true" : "false"} + ", ^^" + kanch +
                 ", ^^" + vanch;
        _wrapper_name = container_ref<C>();
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
        _doc->record_type_name(key, std::string{ordered ? "Map" : "UMap"} +
                                        kname + vname);

        // key/value piece reuse: the SAME conversion source as params/setters
        append_one_param<map_key_type(C), ::welder::naming::none>(_key_pieces, 0,
                                                                  "key");
        append_one_param<map_value_type(C), ::welder::naming::none>(_value_pieces, 1,
                                                                    "value");
        _key_wire = wire_param_v<map_key_type(C)>;
        _value_wire = wire_param_v<map_value_type(C)>;
        _key_pinvoke = pinvoke_type<map_key_type(C), ::welder::naming::none>(false);
        _value_pinvoke = pinvoke_type<map_value_type(C), ::welder::naming::none>(false);
        constexpr bool v_is_handle{classify(map_value_type(C)) ==
                                   marshal_kind::handle};
        _value_get_return = v_is_handle ? std::string{"IntPtr"}
                                : pinvoke_type<map_value_type(C),
                                               ::welder::naming::none>(true);
        _value_get_wire = v_is_handle
                         ? std::string{"void*"}
                         : std::string{wire_return_v<map_value_type(C)>};
        for (const char* leaf : {"_new", "_destroy", "_size", "_contains",
                                 "_get", "_set", "_remove", "_clear"})
            _doc->record_symbol(_symbol_stem + leaf);
        _key_import_attr = import_attr(_key_pieces.has_string);
        _key_value_import_attr = import_attr(_key_pieces.has_string || _value_pieces.has_string);
        _key_public = public_type<map_key_type(C), ::welder::naming::none>();
        _value_public = public_type<map_value_type(C), ::welder::naming::none>();
        _get_body = wrapper_return_body<map_value_type(C),
                                        ::welder::naming::none,
                                        field_return_policy(
                                            map_value_type(C))>(
            "NativeMethods." + _symbol_stem + "_get(_h_" + _wrapper_name + ", " +
                _key_pieces.wrapper_args + ", out WelderError _e)",
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
        code_writer t{_doc->shim, 0};
        t.line("void* {}_new(welder_error* err) { return "
               "wcs::shim::map_new<{}>(err); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::map_destroy<{}>"
               "(self); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("std::int64_t {}_size(void* self, welder_error* err) { "
               "return wcs::shim::map_size<{}>(self, err); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("bool {}_contains(void* self, {} k, welder_error* err) { "
               "return wcs::shim::map_contains<{}>(self, k, err); }",
               _symbol_stem, _key_wire, _template_args);
        t.blank();
        t.line("{} {}_get(void* self, {} k, welder_error* err) { return "
               "wcs::shim::map_get<{}>(self, k, err); }",
               _value_get_wire, _symbol_stem, _key_wire, _template_args);
        t.blank();
        t.line("void {}_set(void* self, {} k, {} v, welder_error* err) { "
               "wcs::shim::map_set<{}>(self, k, v, err); }",
               _symbol_stem, _key_wire, _value_wire, _template_args);
        t.blank();
        t.line("bool {}_remove(void* self, {} k, welder_error* err) { "
               "return wcs::shim::map_remove<{}>(self, k, err); }",
               _symbol_stem, _key_wire, _template_args);
        t.blank();
        t.line("void {}_clear(void* self, welder_error* err) { "
               "wcs::shim::map_clear<{}>(self, err); }",
               _symbol_stem, _template_args);
        t.blank();
    }

    /** Write the `[LibraryImport]` declarations for the op thunks (UTF-8
        marshalling attributes when a string key/value crosses; `bool` results
        as `U1`). */
    void emit_pinvokes() {
        code_writer p{_doc->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_new(out WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_destroy(IntPtr self);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial long {}"
               "_size({}Handle self, out WelderError err);",
               _symbol_stem, _wrapper_name);
        p.line("{} [return: MarshalAs(UnmanagedType.U1)] internal static "
               "partial bool {}_contains({}Handle self, {} k, out "
               "WelderError err);",
               _key_import_attr, _symbol_stem, _wrapper_name, _key_pinvoke);
        p.line("{} internal static partial {} {}_get({}Handle self, {} k, "
               "out WelderError err);",
               _key_import_attr, _value_get_return, _symbol_stem, _wrapper_name, _key_pinvoke);
        p.line("{} internal static partial void {}_set({}Handle self, {} "
               "k, {} v, out WelderError err);",
               _key_value_import_attr, _symbol_stem, _wrapper_name, _key_pinvoke, _value_pinvoke);
        p.line("{} [return: MarshalAs(UnmanagedType.U1)] internal static "
               "partial bool {}_remove({}Handle self, {} k, out "
               "WelderError err);",
               _key_import_attr, _symbol_stem, _wrapper_name, _key_pinvoke);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_clear({}Handle self, out WelderError err);",
               _symbol_stem, _wrapper_name);
    }

    /** Write the managed side: the `SafeHandle` owning the native map and the
        public wrapper class — `Count`, `ContainsKey`, the `this[K]` indexer
        (insert-or-assign on set), `Remove`, `Clear`, `Dispose`. */
    void emit_wrapper() {
        code_writer w{_doc->containers, 1};
        w.line("internal sealed class {}Handle : SafeHandle", _wrapper_name);
        {
            const auto cls{w.braces()};
            w.line("internal {}Handle(IntPtr handle, bool owns) : "
                   "base(IntPtr.Zero, owns)",
                   _wrapper_name);
            {
                const auto body{w.braces()};
                w.line("SetHandle(handle);");
            }
            w.line("public override bool IsInvalid => handle == IntPtr.Zero;");
            w.line("protected override bool ReleaseHandle()");
            {
                const auto body{w.braces()};
                w.line("NativeMethods.{}_destroy(handle);", _symbol_stem);
                w.line("return true;");
            }
        }
        w.blank();
        w.line("/// <summary>A reference-semantic C++ {} of {} to "
               "{}.</summary>",
               _ordered ? "std::map" : "std::unordered_map", _key_public, _value_public);
        w.line("public sealed class {} : IDisposable", _wrapper_name);
        {
            const auto cls{w.braces()};
            w.line("internal {}Handle _h_{};", _wrapper_name, _wrapper_name);
            w.line("internal object? _owner;");
            w.line("internal {}(IntPtr handle, bool owns) { _h_{} = new "
                   "{}Handle(handle, owns); }",
                   _wrapper_name, _wrapper_name, _wrapper_name);
            // An empty C# body is a literal "{}" — an argument, never format
            // text (cat would eat it as a placeholder).
            w.line("public {}() : this(_New(), true) {}", _wrapper_name, "{}");
            w.line("private static IntPtr _New()");
            {
                const auto body{w.braces()};
                w.line("IntPtr _r = NativeMethods.{}_new(out WelderError _e);",
                       _symbol_stem);
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
                           _symbol_stem, _wrapper_name);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                    w.line("return (int)_r;");
                }
            }
            w.line("public bool ContainsKey({} key)", _key_public);
            {
                const auto body{w.braces()};
                w.line("var _r = NativeMethods.{}_contains(_h_{}, {}, out "
                       "WelderError _e);",
                       _symbol_stem, _wrapper_name, _key_pieces.wrapper_args);
                w.line("WelderInterop.ThrowIfError(in _e);");
                w.line("return _r;");
            }
            w.line("public {} this[{} key]", _value_public, _key_public);
            {
                const auto prop{w.braces()};
                w.line("get");
                {
                    const auto arm{w.braces()};
                    w.raw(_get_body);
                }
                w.line("set");
                {
                    const auto arm{w.braces()};
                    w.line("NativeMethods.{}_set(_h_{}, {}, out "
                           "WelderError _e);",
                           _symbol_stem, _wrapper_name, _key_pieces.wrapper_args + _value_pieces.wrapper_args);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                }
            }
            w.line("public bool Remove({} key)", _key_public);
            {
                const auto body{w.braces()};
                w.line("var _r = NativeMethods.{}_remove(_h_{}, {}, out "
                       "WelderError _e);",
                       _symbol_stem, _wrapper_name, _key_pieces.wrapper_args);
                w.line("WelderInterop.ThrowIfError(in _e);");
                w.line("return _r;");
            }
            w.line("public void Clear()");
            {
                const auto body{w.braces()};
                w.line("NativeMethods.{}_clear(_h_{}, out WelderError _e);",
                       _symbol_stem, _wrapper_name);
                w.line("WelderInterop.ThrowIfError(in _e);");
            }
            w.line("public void Dispose() => _h_{}.Dispose();", _wrapper_name);
        }
        w.blank();
    }

    document* _doc;       /**< The shared document. */
    bool _ordered{false}; /**< `std::map` vs `std::unordered_map`. */
    /** The C symbol stem (`welder_[u]map_<k>_<v>`). */
    std::string _symbol_stem{};
    /** The shim's template arguments (`ordered, ^^key, ^^value`). */
    std::string _template_args{};
    /** The wrapper class's name reference. */
    std::string _wrapper_name{};
    /** The key's C-ABI parameter spelling. */
    std::string _key_wire{};
    /** The value's C-ABI parameter spelling. */
    std::string _value_wire{};
    /** The key's P/Invoke parameter type. */
    std::string _key_pinvoke{};
    /** The value's P/Invoke parameter type. */
    std::string _value_pinvoke{};
    /** `_get`'s P/Invoke return type (a handle value reads as `IntPtr` — a
        live view). */
    std::string _value_get_return{};
    /** `_get`'s C-ABI return spelling. */
    std::string _value_get_wire{};
    /** The key-only `[LibraryImport]` attribute. */
    std::string _key_import_attr{};
    /** The key+value `[LibraryImport]` attribute. */
    std::string _key_value_import_attr{};
    /** The key's public C# type. */
    std::string _key_public{};
    /** The value's public C# type. */
    std::string _value_public{};
    /** The indexer getter's pre-indented body. */
    std::string _get_body{};
    /** The key's conversion pieces. */
    call_pieces _key_pieces{};
    /** The value's conversion pieces. */
    call_pieces _value_pieces{};
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
