#pragma once
#include <cctype>
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/containers/element.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The fixed-size sibling of `<welder/rods/csharp/emit/containers/vector.hpp>`:
    a **`std::array` of a welded class**.

    The same live-view element protocol, minus every size-changing operation —
    `Count` is a compile-time constant, and there is no `Add` or `Clear`.
*/

namespace welder::inline v0::rods::csharp {

/** The fixed-size sibling of @ref ensure_vector: `std::array<welded, N>` (or
    of a nested sequence)
    — the vector protocol minus the size-changing ops (constant `Count`,
    live-view indexer with write-through set). */
template <std::meta::info C>
void ensure_fixed(document& doc) {
    static constexpr const char* key{
        std::define_static_string(std::meta::display_string_of(C))};
    if (!doc.claim_container(key))
        return;
    constexpr std::size_t n{fixed_extent(C)};
    const std::string ns{std::to_string(n)};
    constexpr std::meta::info El{
        std::meta::remove_cvref(sequence_element(C))};
    constexpr bool welded_elem{classify(El) == marshal_kind::handle};
    ensure_element_wrapper<El>(doc);
    const std::string sym{std::string{"welder_arr"} + ns + "_" + symtok_v<El>};
    const std::string targs{"^^" + element_cpp_spelling<El>() + ", " + ns};
    const std::string V{container_ref<C>()};
    const std::string E{welded_elem ? type_ref<El>() : container_ref<El>()};
    const std::string Ef{welded_elem ? field_ref<El>() : container_ref<El>()};
    doc.record_type_name(key, "Array" + Ef + "x" + ns);
    for (const char* leaf : {"_new", "_destroy", "_get", "_set"})
        doc.record_symbol(sym + leaf);
    doc.shim +=
        "void* " + sym + "_new(welder_error* err) { return "
        "wcs::shim::arr_new<" + targs + ">(err); }\n\n"
        "void " + sym + "_destroy(void* self) { wcs::shim::arr_destroy<" +
        targs + ">(self); }\n\n"
        "void* " + sym + "_get(void* self, std::int64_t i, welder_error* "
        "err) { return wcs::shim::arr_get<" + targs + ">(self, i, err); }\n\n"
        "void " + sym + "_set(void* self, std::int64_t i, void* elem, "
        "welder_error* err) { wcs::shim::arr_set<" + targs +
        ">(self, i, elem, err); }\n\n";
    doc.pinvoke +=
        "        [LibraryImport(Lib)] internal static partial IntPtr " +
        sym + "_new(out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_destroy(IntPtr self);\n"
        "        [LibraryImport(Lib)] internal static partial IntPtr " +
        sym + "_get(" + V + "Handle self, long i, out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_set(" + V + "Handle self, long i, " + E +
        "Handle elem, out WelderError err);\n";
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
        "    /// <summary>A reference-semantic C++ std::array of " + ns +
        " " + E + " (live element views; fixed size).</summary>\n"
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
        "        public int Count => " + ns + ";\n"
        "        public " + E + " this[int i]\n        {\n"
        "            get\n            {\n"
        "                IntPtr _r = NativeMethods." + sym + "_get(_h_" +
        V + ", i, out WelderError _e);\n"
        "                WelderInterop.ThrowIfError(in _e);\n"
        "                var _v = new " + E + "(_r, false);\n"
        "                _v._owner = this;\n"
        "                return _v;\n            }\n"
        "            set\n            {\n"
        "                NativeMethods." + sym + "_set(_h_" + V + ", i, "
        "value._h_" + Ef + ", out WelderError _e);\n"
        "                WelderInterop.ThrowIfError(in _e);\n"
        "            }\n        }\n"
        "        public void Dispose() => _h_" + V + ".Dispose();\n"
        "    }\n\n";
}
} // namespace welder::inline v0::rods::csharp
