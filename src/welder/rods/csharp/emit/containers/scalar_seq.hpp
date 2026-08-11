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
    The generated wrapper for a **scalar or enum sequence used as a live field**
    (`std::vector<int>` / `std::array<double, 3>` members).

    Params and returns of these types cross by value as `T[]` copies, which is
    the ergonomic choice; a non-const *field* cannot, because a snapshot would
    make `obj.Nums.Add(…)` silently mutate a temporary. So a field binds to this
    wrapper instead: `Count`, an indexer, `Add`/`Clear` (vector only), bulk
    `CopyFrom`, an implicit conversion from `T[]` so whole-property assignment
    still reads naturally — and, the point of the exercise, `AsSpan()`.

    `Span<T>` **is** C#'s buffer protocol, so `AsSpan()` over the C++ buffer is
    the .NET analogue of the Python rods' numpy view: zero-copy, and valid until
    a size-changing operation or `Dispose` — exactly a C++ iterator's rule.
*/

namespace welder::inline v0::rods::csharp {

/** The reference-semantic wrapper for a SCALAR/ENUM sequence used as a
    live field (`std::vector<int>` / `std::array<double, 3>` members):
    Count, a bounds-checked indexer, `Add`/`Clear` (vector only), bulk
    `CopyFrom`, an implicit conversion from `T[]` (so whole-property
    assignment reads naturally), and — the zero-copy path — `AsSpan()`
    over the C++ buffer (`Span<T>` IS C#'s buffer protocol; valid until a
    size-changing operation or Dispose, exactly a C++ iterator's rule). */
template <std::meta::info C>
void ensure_scalar_seq(document& doc) {
    static constexpr const char* key{
        std::define_static_string(std::meta::display_string_of(C))};
    if (!doc.claim_container(key))
        return;
    constexpr std::meta::info E{
        std::meta::dealias(sequence_element(C))};
    constexpr bool fixed{is_fixed_sequence(C)};
    constexpr bool enum_elem{classify(E) == marshal_kind::enum_};
    // the element's C# spelling / name token / wire spelling
    std::string ecs{}, ename{};
    if constexpr (enum_elem) {
        ecs = type_ref<bare(E)>();
        ename = field_ref<bare(E)>();
    } else {
        constexpr const char* c{scalar_spell(E).cs};
        ecs = c;
        ename = c;
        ename[0] = static_cast<char>(std::toupper(ename[0]));
    }
    static constexpr const char* tok{std::define_static_string(
        map_token(E))};
    static constexpr const char* wire{std::define_static_string(
        enum_elem ? std::string{enum_wire_spell(E).c_abi}
                  : std::string{scalar_spell(E).c_abi})};
    static constexpr const char* wire_cs{std::define_static_string(
        enum_elem ? std::string{enum_wire_spell(E).cs}
                  : std::string{scalar_spell(E).cs})};
    static constexpr const char* ecpp{
        std::define_static_string(leaf_cpp_spelling(E))};
    std::string ns{};
    if constexpr (fixed)
        ns = std::to_string(fixed_extent(C));
    const std::string sym{fixed ? "welder_arrs_" + std::string{tok} + "_" + ns
                                : "welder_vecs_" + std::string{tok}};
    const std::string targs{fixed ? "^^" + std::string{ecpp} + ", " + ns
                                  : "^^" + std::string{ecpp}};
    const std::string V{fixed ? "Array" + ename + "x" + ns
                              : "Vector" + ename};
    doc.record_type_name(key, V);
    const char* pfx{fixed ? "arr" : "vec"};
    for (const char* leaf : {"_new", "_destroy", "_data", "_fill"})
        doc.record_symbol(sym + leaf);
    doc.shim +=
        "void* " + sym + "_new(welder_error* err) { return wcs::shim::" +
        pfx + "_new<" + targs + ">(err); }\n\n"
        "void " + sym + "_destroy(void* self) { wcs::shim::" + pfx +
        "_destroy<" + targs + ">(self); }\n\n"
        "void* " + sym + "_data(void* self, welder_error* err) { return "
        "wcs::shim::" + pfx + "_data<" + targs + ">(self, err); }\n\n"
        "void " + sym + "_fill(void* self, const void* data, std::int64_t "
        "len, welder_error* err) { wcs::shim::" + pfx + "_fill<" + targs +
        ">(self, data, len, err); }\n\n";
    if constexpr (!fixed) {
        for (const char* leaf : {"_size", "_push", "_clear"})
            doc.record_symbol(sym + leaf);
        doc.shim +=
            "std::int64_t " + sym + "_size(void* self, welder_error* err) "
            "{ return wcs::shim::vec_size<" + targs + ">(self, err); }\n\n"
            "void " + sym + "_push(void* self, " + wire +
            " v, welder_error* err) { wcs::shim::vec_push<" + targs +
            ">(self, v, err); }\n\n"
            "void " + sym + "_clear(void* self, welder_error* err) { "
            "wcs::shim::vec_clear<" + targs + ">(self, err); }\n\n";
    }
    doc.pinvoke +=
        "        [LibraryImport(Lib)] internal static partial IntPtr " +
        sym + "_new(out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_destroy(IntPtr self);\n"
        "        [LibraryImport(Lib)] internal static partial IntPtr " +
        sym + "_data(" + V + "Handle self, out WelderError err);\n"
        "        [LibraryImport(Lib)] internal static partial void " + sym +
        "_fill(" + V + "Handle self, IntPtr data, long len, out "
        "WelderError err);\n";
    if constexpr (!fixed)
        doc.pinvoke +=
            "        [LibraryImport(Lib)] internal static partial long " +
            sym + "_size(" + V + "Handle self, out WelderError err);\n"
            "        [LibraryImport(Lib)] internal static partial void " +
            sym + "_push(" + V + "Handle self, " + wire_cs +
            " v, out WelderError err);\n"
            "        [LibraryImport(Lib)] internal static partial void " +
            sym + "_clear(" + V + "Handle self, out WelderError err);\n";
    std::string count_body{};
    if constexpr (fixed)
        count_body = "        public int Count => " + ns + ";\n";
    else
        count_body =
            "        public int Count\n        {\n            get\n"
            "            {\n"
            "                var _r = NativeMethods." + sym + "_size(_h_" +
            V + ", out WelderError _e);\n"
            "                WelderInterop.ThrowIfError(in _e);\n"
            "                return (int)_r;\n            }\n        }\n";
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
        (fixed ? "std::array of " + ns + " " : "vector of ") + ecs +
        " (live element access; AsSpan() is a zero-copy view over the C++ "
        "buffer, valid until a size-changing operation or "
        "Dispose).</summary>\n"
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
        "            return _r;\n        }\n" +
        count_body +
        "        public unsafe Span<" + ecs + "> AsSpan()\n        {\n"
        "            IntPtr _d = NativeMethods." + sym + "_data(_h_" + V +
        ", out WelderError _e);\n"
        "            WelderInterop.ThrowIfError(in _e);\n"
        "            var _s = new Span<" + ecs + ">((void*)_d, Count);\n"
        "            GC.KeepAlive(this);\n"
        "            return _s;\n        }\n"
        "        public " + ecs + " this[int i]\n        {\n"
        "            get => AsSpan()[i];\n"
        "            set => AsSpan()[i] = value;\n        }\n" +
        (fixed ? std::string{}
               : "        public void Add(" + ecs + " item)\n        {\n"
                 "            NativeMethods." + sym + "_push(_h_" + V +
                 ", " +
                 (enum_elem ? "(" + std::string{wire_cs} + ")item"
                            : std::string{"item"}) +
                 ", out WelderError _e);\n"
                 "            WelderInterop.ThrowIfError(in _e);\n"
                 "        }\n"
                 "        public void Clear()\n        {\n"
                 "            NativeMethods." + sym + "_clear(_h_" + V +
                 ", out WelderError _e);\n"
                 "            WelderInterop.ThrowIfError(in _e);\n"
                 "        }\n") +
        "        public " + ecs + "[] ToArray() => AsSpan().ToArray();\n"
        "        public unsafe void CopyFrom(ReadOnlySpan<" + ecs +
        "> src)\n        {\n"
        "            fixed (" + ecs + "* _p = src)\n            {\n"
        "                NativeMethods." + sym + "_fill(_h_" + V +
        ", (IntPtr)_p, src.Length, out WelderError _e);\n"
        "                WelderInterop.ThrowIfError(in _e);\n"
        "            }\n        }\n"
        "        public static implicit operator " + V + "(" + ecs +
        "[] a)\n        {\n"
        "            var _v = new " + V + "();\n"
        "            _v.CopyFrom(a);\n"
        "            return _v;\n        }\n"
        "        public void Dispose() => _h_" + V + ".Dispose();\n"
        "    }\n\n";
}
} // namespace welder::inline v0::rods::csharp
