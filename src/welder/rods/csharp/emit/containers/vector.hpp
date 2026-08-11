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
    The generated wrapper for a **`std::vector` of a welded class**: welder's
    opaque-container model, managed-side.

    The wrapper holds a handle to the C++ vector, and its indexer hands out a
    **live view** of the element — pinned to the wrapper, so the vector cannot be
    collected while a view of one of its elements is alive. That is what makes
    `v[i].Field = x` write through, exactly as `bind_vector` does for the Python
    rods, instead of mutating a copy. Writes go the other way: `Add` and the
    indexer's setter copy-assign from a borrowed handle.
*/

namespace welder::inline v0::rods::csharp {

/** Generate the reference-semantic wrapper for `std::vector<welded>`
    container type @a C (once per distinct instantiation): the native op
    thunks (delegating into `shim::vec_*`), their P/Invokes, the rename
    registration and the C# wrapper class (live element views pinned to
    the vector wrapper — welder's opaque-container model). */
template <std::meta::info C>
void ensure_vector(document& doc) {
    static constexpr const char* key{
        std::define_static_string(std::meta::display_string_of(C))};
    if (!doc.claim_container(key))
        return;
    const std::string sym{std::string{"welder_vec_"} +
                          symtok_v<bare(sequence_element(C))>};
    const std::string eq{"^^" + anchor_ref<bare(sequence_element(C))>()};
    const std::string V{container_ref<C>()};
    const std::string E{type_ref<bare(sequence_element(C))>()};
    const std::string Ef{field_ref<bare(sequence_element(C))>()};
    // The wrapper's NAME must be an identifier, so it derives from the
    // element's identifier-safe form (a nested element's dots sanitize).
    doc.record_type_name(key, "Vector" + Ef);
    for (const char* leaf : {"_new", "_destroy", "_size", "_get", "_set",
                             "_add", "_clear"})
        doc.record_symbol(sym + leaf);
    doc.shim +=
        "void* " + sym + "_new(welder_error* err) { return "
        "wcs::shim::vec_new<" + eq + ">(err); }\n\n"
        "void " + sym + "_destroy(void* self) { wcs::shim::vec_destroy<" +
        eq + ">(self); }\n\n"
        "std::int64_t " + sym + "_size(void* self, welder_error* err) { "
        "return wcs::shim::vec_size<" + eq + ">(self, err); }\n\n"
        "void* " + sym + "_get(void* self, std::int64_t i, welder_error* "
        "err) { return wcs::shim::vec_get<" + eq + ">(self, i, err); }\n\n"
        "void " + sym + "_set(void* self, std::int64_t i, void* elem, "
        "welder_error* err) { wcs::shim::vec_set<" + eq +
        ">(self, i, elem, err); }\n\n"
        "void " + sym + "_add(void* self, void* elem, welder_error* err) { "
        "wcs::shim::vec_add<" + eq + ">(self, elem, err); }\n\n"
        "void " + sym + "_clear(void* self, welder_error* err) { "
        "wcs::shim::vec_clear<" + eq + ">(self, err); }\n\n";
    doc.pinvoke +=
        "        [LibraryImport(Lib)] internal static partial IntPtr " +
        sym + "_new(out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_destroy(IntPtr self);\n"
        "        [LibraryImport(Lib)] internal static partial long " + sym +
        "_size(" + V + "Handle self, out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial IntPtr " +
        sym + "_get(" + V + "Handle self, long i, out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_set(" + V + "Handle self, long i, " + E +
        "Handle elem, out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_add(" + V + "Handle self, " + E +
        "Handle elem, out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_clear(" + V + "Handle self, out WelderError err);\n";
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
        "    /// <summary>A reference-semantic C++ vector of " + E +
        " (live element views).</summary>\n"
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
        "        public void Add(" + E + " item)\n        {\n"
        "            NativeMethods." + sym + "_add(_h_" + V + ", item._h_" +
        Ef + ", out WelderError _e);\n"
        "            WelderInterop.ThrowIfError(in _e);\n        }\n"
        "        public void Clear()\n        {\n"
        "            NativeMethods." + sym + "_clear(_h_" + V +
        ", out WelderError _e);\n"
        "            WelderInterop.ThrowIfError(in _e);\n        }\n"
        "        public void Dispose() => _h_" + V + ".Dispose();\n"
        "    }\n\n";
}
} // namespace welder::inline v0::rods::csharp
