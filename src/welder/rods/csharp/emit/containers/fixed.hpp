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
    @ref welder::rods::csharp::fixed_wrapper_emitter is the component.

    The same live-view element protocol, minus every size-changing operation —
    `Count` is a compile-time constant, and there is no `Add` or `Clear`.
*/

namespace welder::inline v0::rods::csharp {

/** The component that generates the fixed-size sibling of
    @ref vector_wrapper_emitter's wrapper: `std::array<welded, N>` (or of a
    nested sequence) — the vector protocol minus the size-changing ops
    (constant `Count`, live-view indexer with write-through set).

    @ref ensure derives the spellings the three artifacts share (the extent
    folds into both the C symbol stem and the wrapper name) and then writes
    them as named steps: op thunks, P/Invokes, wrapper class. */
class fixed_wrapper_emitter {
  public:
    /** Bind the emitter to the growing document.
        @param doc the two-artifact document. */
    explicit fixed_wrapper_emitter(document& doc) : _doc{&doc} {}

    /** Generate the wrapper for array specialization @a C, if this is the
        first time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the `std::array` specialization. */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!_doc->claim_container(key))
            return;
        constexpr std::size_t n{fixed_extent(C)};
        _extent_text = std::to_string(n);
        constexpr std::meta::info El{
            std::meta::remove_cvref(sequence_element(C))};
        constexpr bool welded_elem{classify(El) == marshal_kind::handle};
        ensure_element_wrapper<El>(*_doc);
        _symbol_stem = std::string{"welder_arr"} + _extent_text + "_" + symtok_v<El>;
        _template_args = "^^" + element_cpp_spelling<El>() + ", " + _extent_text;
        _wrapper_name = container_ref<C>();
        _element_ref = welded_elem ? type_ref<El>() : container_ref<El>();
        _element_field_ref = welded_elem ? field_ref<El>() : container_ref<El>();
        _doc->record_type_name(key, "Array" + _element_field_ref + "x" + _extent_text);
        for (const char* leaf : {"_new", "_destroy", "_get", "_set"})
            _doc->record_symbol(_symbol_stem + leaf);
        emit_thunks();
        emit_pinvokes();
        emit_wrapper();
    }

  private:
    /** Write the native op thunks — one-line delegations into the compiled
        `shim::arr_*` support templates, parameterized by element and extent. */
    void emit_thunks() {
        code_writer t{_doc->shim, 0};
        t.line("void* {}_new(welder_error* err) { return "
               "wcs::shim::arr_new<{}>(err); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::arr_destroy<{}>"
               "(self); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("void* {}_get(void* self, std::int64_t i, welder_error* "
               "err) { return wcs::shim::arr_get<{}>(self, i, err); }",
               _symbol_stem, _template_args);
        t.blank();
        t.line("void {}_set(void* self, std::int64_t i, void* elem, "
               "welder_error* err) { wcs::shim::arr_set<{}>"
               "(self, i, elem, err); }",
               _symbol_stem, _template_args);
        t.blank();
    }

    /** Write the `[LibraryImport]` declarations for the op thunks. */
    void emit_pinvokes() {
        code_writer p{_doc->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_new(out WelderError err);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_destroy(IntPtr self);",
               _symbol_stem);
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_get({}Handle self, long i, out WelderError err);",
               _symbol_stem, _wrapper_name);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_set({}Handle self, long i, {}Handle elem, out WelderError "
               "err);",
               _symbol_stem, _wrapper_name, _element_ref);
    }

    /** Write the managed side: the `SafeHandle` owning the native array and
        the public wrapper class — constant `Count`, a live-view indexer with
        write-through set, `Dispose`. */
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
        w.line("/// <summary>A reference-semantic C++ std::array of {} {} "
               "(live element views; fixed size).</summary>",
               _extent_text, _element_ref);
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
            w.line("public int Count => {};", _extent_text);
            w.line("public {} this[int i]", _element_ref);
            {
                const auto prop{w.braces()};
                w.line("get");
                {
                    const auto arm{w.braces()};
                    w.line("IntPtr _r = NativeMethods.{}_get(_h_{}, i, out "
                           "WelderError _e);",
                           _symbol_stem, _wrapper_name);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                    w.line("var _v = new {}(_r, false);", _element_ref);
                    w.line("_v._owner = this;");
                    w.line("return _v;");
                }
                w.line("set");
                {
                    const auto arm{w.braces()};
                    w.line("NativeMethods.{}_set(_h_{}, i, value._h_{}, out "
                           "WelderError _e);",
                           _symbol_stem, _wrapper_name, _element_field_ref);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                }
            }
            w.line("public void Dispose() => _h_{}.Dispose();", _wrapper_name);
        }
        w.blank();
    }

    document* _doc; /**< The shared document. */
    /** The extent `N`, as text. */
    std::string _extent_text{};
    /** The C symbol stem (`welder_arr<N>_<eltok>`). */
    std::string _symbol_stem{};
    /** The shim's template arguments (`^^element, N`). */
    std::string _template_args{};
    /** The wrapper class's name reference. */
    std::string _wrapper_name{};
    /** The element's C# reference (view type). */
    std::string _element_ref{};
    /** The element's identifier-safe (field) form. */
    std::string _element_field_ref{};
};

/** The fixed-size sibling of @ref ensure_vector — `std::array<welded, N>` (or
    of a nested sequence)
    — the vector protocol minus the size-changing ops (constant `Count`,
    live-view indexer with write-through set). Forwards into
    @ref welder::rods::csharp::fixed_wrapper_emitter.
    @tparam C a reflection of the `std::array` specialization.
    @param doc the growing document. */
template <std::meta::info C>
void ensure_fixed(document& doc) {
    fixed_wrapper_emitter{doc}.ensure<C>();
}

} // namespace welder::inline v0::rods::csharp
