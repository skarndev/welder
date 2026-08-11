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

    A welded mapped type reads as a **live view** pinned to the map wrapper, on
    the same rationale as the sequence wrappers; a leaf mapped type crosses by
    value. Only the default-argument map form is admitted — a custom comparator
    or hasher makes the re-derived spelling a different type, so those are a
    designed `classify` rejection rather than a silently wrong cast.
*/

namespace welder::inline v0::rods::csharp {

/** The reference-semantic map wrapper (`std::map`/`std::unordered_map`
    with a leaf key): Count, ContainsKey, a `this[K]` indexer (a live view
    for a welded mapped type, a value copy otherwise; insert-or-assign on
    set), Remove, Clear. */
template <std::meta::info C>
void ensure_map(document& doc) {
    static constexpr const char* key{
        std::define_static_string(std::meta::display_string_of(C))};
    if (!doc.claim_container(key))
        return;
    constexpr bool ordered{is_specialization_of(C, ^^std::map)};
    static constexpr const char* ktok{
        std::define_static_string(map_token(map_key_type(C)))};
    static constexpr const char* vtok{
        std::define_static_string(map_token(map_value_type(C)))};
    const std::string sym{std::string{ordered ? "welder_map_" : "welder_umap_"} +
                          ktok + "_" + vtok};
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
    const std::string targs{std::string{ordered ? "true" : "false"} +
                            ", ^^" + kanch + ", ^^" + vanch};
    const std::string V{container_ref<C>()};
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
    doc.record_type_name(key, std::string{ordered ? "Map" : "UMap"} +
                                  kname + vname);

    // key/value piece reuse: the SAME conversion source as params/setters
    call_pieces kcp{};
    append_one_param<map_key_type(C), ::welder::naming::none>(kcp, 0,
                                                              "key");
    call_pieces vcp{};
    append_one_param<map_value_type(C), ::welder::naming::none>(vcp, 1,
                                                                "value");
    const std::string kwire{wire_param_v<map_key_type(C)>};
    const std::string vwire{wire_param_v<map_value_type(C)>};
    const std::string kpin{pinvoke_type<map_key_type(C),
                                        ::welder::naming::none>(false)};
    const std::string vpin{pinvoke_type<map_value_type(C),
                                        ::welder::naming::none>(false)};
    constexpr bool v_is_handle{classify(map_value_type(C)) ==
                               marshal_kind::handle};
    const std::string vget_ret{
        v_is_handle ? std::string{"IntPtr"}
                    : pinvoke_type<map_value_type(C),
                                   ::welder::naming::none>(true)};
    const std::string vget_wire{
        v_is_handle ? std::string{"void*"}
                    : std::string{wire_return_v<map_value_type(C)>}};
    for (const char* leaf : {"_new", "_destroy", "_size", "_contains",
                             "_get", "_set", "_remove", "_clear"})
        doc.record_symbol(sym + leaf);
    doc.shim +=
        "void* " + sym + "_new(welder_error* err) { return "
        "wcs::shim::map_new<" + targs + ">(err); }\n\n"
        "void " + sym + "_destroy(void* self) { wcs::shim::map_destroy<" +
        targs + ">(self); }\n\n"
        "std::int64_t " + sym + "_size(void* self, welder_error* err) { "
        "return wcs::shim::map_size<" + targs + ">(self, err); }\n\n"
        "bool " + sym + "_contains(void* self, " + kwire +
        " k, welder_error* err) { return wcs::shim::map_contains<" + targs +
        ">(self, k, err); }\n\n" +
        vget_wire + " " + sym + "_get(void* self, " + kwire +
        " k, welder_error* err) { return wcs::shim::map_get<" + targs +
        ">(self, k, err); }\n\n"
        "void " + sym + "_set(void* self, " + kwire + " k, " + vwire +
        " v, welder_error* err) { wcs::shim::map_set<" + targs +
        ">(self, k, v, err); }\n\n"
        "bool " + sym + "_remove(void* self, " + kwire +
        " k, welder_error* err) { return wcs::shim::map_remove<" + targs +
        ">(self, k, err); }\n\n"
        "void " + sym + "_clear(void* self, welder_error* err) { "
        "wcs::shim::map_clear<" + targs + ">(self, err); }\n\n";
    const std::string kattr{import_attr(kcp.has_string)};
    const std::string kvattr{
        import_attr(kcp.has_string || vcp.has_string)};
    doc.pinvoke +=
        "        [LibraryImport(Lib)] internal static partial IntPtr " +
        sym + "_new(out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_destroy(IntPtr self);\n"
        "        [LibraryImport(Lib)] internal static partial long " + sym +
        "_size(" + V + "Handle self, out WelderError err);\n"
        "        " + kattr + " [return: MarshalAs(UnmanagedType.U1)] "
        "internal static partial bool " + sym + "_contains(" + V +
        "Handle self, " + kpin + " k, out WelderError err);\n"
        "        " + kattr + " internal static partial " + vget_ret + " " +
        sym + "_get(" + V + "Handle self, " + kpin +
        " k, out WelderError err);\n"
        "        " + kvattr + " internal static partial void " + sym +
        "_set(" + V + "Handle self, " + kpin + " k, " + vpin +
        " v, out WelderError err);\n"
        "        " + kattr + " [return: MarshalAs(UnmanagedType.U1)] "
        "internal static partial bool " + sym + "_remove(" + V +
        "Handle self, " + kpin + " k, out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_clear(" + V + "Handle self, out WelderError err);\n";
    const std::string kpub{public_type<map_key_type(C),
                                       ::welder::naming::none>()};
    const std::string vpub{public_type<map_value_type(C),
                                       ::welder::naming::none>()};
    std::string get_body{wrapper_return_body<map_value_type(C),
                                             ::welder::naming::none,
                                             field_return_policy(
                                                 map_value_type(C))>(
        "NativeMethods." + sym + "_get(_h_" + V + ", " + kcp.wrapper_args +
            ", out WelderError _e)",
        "                ", "this")};
    doc.containers +=
        "    internal sealed class " + V + "Handle : SafeHandle\n    {\n"
        "        internal " + V + "Handle(IntPtr handle, bool owns) : "
        "base(IntPtr.Zero, owns)\n        {\n            "
        "SetHandle(handle);\n        }\n"
        "        public override bool IsInvalid => handle == "
        "IntPtr.Zero;\n"
        "        protected override bool ReleaseHandle()\n        {\n"
        "            NativeMethods." + sym + "_destroy(handle);\n"
        "            return true;\n        }\n    }\n\n"
        "    /// <summary>A reference-semantic C++ " +
        (ordered ? "std::map" : "std::unordered_map") + " of " + kpub +
        " to " + vpub + ".</summary>\n"
        "    public sealed class " + V + " : IDisposable\n    {\n"
        "        internal " + V + "Handle _h_" + V + ";\n"
        "        internal object? _owner;\n"
        "        internal " + V + "(IntPtr handle, bool owns) { _h_" + V +
        " = new " + V + "Handle(handle, owns); }\n"
        "        public " + V + "() : this(_New(), true) {}\n"
        "        private static IntPtr _New()\n        {\n"
        "            IntPtr _r = NativeMethods." + sym +
        "_new(out WelderError _e);\n"
        "            WelderInterop.ThrowIfError(in _e);\n"
        "            return _r;\n        }\n"
        "        public int Count\n        {\n            get\n"
        "            {\n"
        "                var _r = NativeMethods." + sym + "_size(_h_" + V +
        ", out WelderError _e);\n"
        "                WelderInterop.ThrowIfError(in _e);\n"
        "                return (int)_r;\n            }\n        }\n"
        "        public bool ContainsKey(" + kpub + " key)\n        {\n"
        "            var _r = NativeMethods." + sym + "_contains(_h_" + V +
        ", " + kcp.wrapper_args + ", out WelderError _e);\n"
        "            WelderInterop.ThrowIfError(in _e);\n"
        "            return _r;\n        }\n"
        "        public " + vpub + " this[" + kpub +
        " key]\n        {\n            get\n            {\n" + get_body +
        "            }\n"
        "            set\n            {\n"
        "                NativeMethods." + sym + "_set(_h_" + V + ", " +
        kcp.wrapper_args + vcp.wrapper_args +
        ", out WelderError _e);\n"
        "                WelderInterop.ThrowIfError(in _e);\n"
        "            }\n        }\n"
        "        public bool Remove(" + kpub + " key)\n        {\n"
        "            var _r = NativeMethods." + sym + "_remove(_h_" + V +
        ", " + kcp.wrapper_args + ", out WelderError _e);\n"
        "            WelderInterop.ThrowIfError(in _e);\n"
        "            return _r;\n        }\n"
        "        public void Clear()\n        {\n"
        "            NativeMethods." + sym + "_clear(_h_" + V +
        ", out WelderError _e);\n"
        "            WelderInterop.ThrowIfError(in _e);\n        }\n"
        "        public void Dispose() => _h_" + V + ".Dispose();\n"
        "    }\n\n";
}
} // namespace welder::inline v0::rods::csharp
