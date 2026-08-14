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
    The generated wrapper for a **`std::vector` of a welded class**: welder's
    opaque-container model, managed-side.
    @ref welder::rods::csharp::vector_wrapper_emitter is the component.

    The wrapper holds a handle to the C++ vector, and its indexer hands out a
    **live view** of the element — pinned to the wrapper, so the vector cannot be
    collected while a view of one of its elements is alive. That is what makes
    `v[i].Field = x` write through, exactly as `bind_vector` does for the Python
    rods, instead of mutating a copy. Writes go the other way: `Add` and the
    indexer's setter copy-assign from a borrowed handle.
*/

namespace welder::inline v0::rods::csharp {

/** The component that generates the reference-semantic wrapper for a
    `std::vector<welded>` — or for a vector whose element is ITSELF a
    sequence — once per distinct instantiation.

    @ref ensure derives the spellings the three artifacts share (the C symbol
    stem, the shim's template argument, the wrapper/element names) and then
    writes them as named steps: the native op thunks (delegating into
    `shim::vec_*`), their P/Invokes, and the C# wrapper class (live element
    views pinned to the vector wrapper — welder's opaque-container model). */
class vector_wrapper_emitter {
  public:
    /** Bind the emitter to the growing document.
        @param doc the two-artifact document. */
    explicit vector_wrapper_emitter(document& doc) : _doc{&doc} {}

    /** Generate the wrapper for vector specialization @a C, if this is the
        first time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the vector specialization. */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!_doc->claim_container(key))
            return;
        constexpr std::meta::info El{
            std::meta::remove_cvref(sequence_element(C))};
        // A container element is referred to by its own generated wrapper
        // (whose name is an identifier already), a welded one by its type
        // reference.
        constexpr bool welded_elem{classify(El) == marshal_kind::handle};
        ensure_element_wrapper<El>(*_doc);
        _symbol_stem = std::string{"welder_vec_"} + symtok_v<El>;
        _element_anchor = "^^" + element_cpp_spelling<El>();
        _wrapper_name = container_ref<C>();
        _element_ref = welded_elem ? type_ref<El>() : container_ref<El>();
        _element_field_ref = welded_elem ? field_ref<El>() : container_ref<El>();
        // The wrapper's NAME must be an identifier, so it derives from the
        // element's identifier-safe form (a nested element's dots sanitize).
        _doc->record_type_name(key, "Vector" + _element_field_ref);
        for (const char* leaf : {"_new", "_destroy", "_size", "_get", "_set",
                                 "_add", "_clear"})
            _doc->record_symbol(_symbol_stem + leaf);
        emit_thunks();
        emit_pinvokes();
        emit_wrapper();
    }

  private:
    /** Write the native op thunks — one-line delegations into the compiled
        `shim::vec_*` support templates, parameterized by the element type. */
    void emit_thunks() {
        code_writer t{_doc->shim, 0};
        t.line("void* {}_new(welder_error* err) { return "
               "wcs::shim::vec_new<{}>(err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::vec_destroy<{}>"
               "(self); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("std::int64_t {}_size(void* self, welder_error* err) { "
               "return wcs::shim::vec_size<{}>(self, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void* {}_get(void* self, std::int64_t i, welder_error* "
               "err) { return wcs::shim::vec_get<{}>(self, i, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void {}_set(void* self, std::int64_t i, void* elem, "
               "welder_error* err) { wcs::shim::vec_set<{}>"
               "(self, i, elem, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void {}_add(void* self, void* elem, welder_error* err) { "
               "wcs::shim::vec_add<{}>(self, elem, err); }",
               _symbol_stem, _element_anchor);
        t.blank();
        t.line("void {}_clear(void* self, welder_error* err) { "
               "wcs::shim::vec_clear<{}>(self, err); }",
               _symbol_stem, _element_anchor);
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
        p.line("[LibraryImport(Lib)] internal static partial long {}"
               "_size({}Handle self, out WelderError err);",
               _symbol_stem, _wrapper_name);
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_get({}Handle self, long i, out WelderError err);",
               _symbol_stem, _wrapper_name);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_set({}Handle self, long i, {}Handle elem, out WelderError "
               "err);",
               _symbol_stem, _wrapper_name, _element_ref);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_add({}Handle self, {}Handle elem, out WelderError err);",
               _symbol_stem, _wrapper_name, _element_ref);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_clear({}Handle self, out WelderError err);",
               _symbol_stem, _wrapper_name);
    }

    /** Write the managed side: the `SafeHandle` owning the native vector and
        the public wrapper class — `Count`, a live-view indexer with
        write-through set, `Add`, `Clear`, `Dispose`. */
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
        w.line("/// <summary>A reference-semantic C++ vector of {} (live "
               "element views).</summary>",
               _element_ref);
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
            w.line("public void Add({} item)", _element_ref);
            {
                const auto body{w.braces()};
                w.line("NativeMethods.{}_add(_h_{}, item._h_{}, out "
                       "WelderError _e);",
                       _symbol_stem, _wrapper_name, _element_field_ref);
                w.line("WelderInterop.ThrowIfError(in _e);");
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

    document* _doc; /**< The shared document. */
    /** The C symbol stem (`welder_vec_<eltok>`). */
    std::string _symbol_stem{};
    /** The shim's template argument (`^^element`). */
    std::string _element_anchor{};
    /** The wrapper class's name reference. */
    std::string _wrapper_name{};
    /** The element's C# reference (view type). */
    std::string _element_ref{};
    /** The element's identifier-safe (field) form. */
    std::string _element_field_ref{};
};

/** Generate the reference-semantic wrapper for `std::vector<welded>` — or for
    a vector whose element is ITSELF a sequence — container type @a C (once per
    distinct instantiation): the native op thunks (delegating into
    `shim::vec_*`), their P/Invokes, the rename registration and the C# wrapper
    class (live element views pinned to the vector wrapper — welder's
    opaque-container model). Forwards into
    @ref welder::rods::csharp::vector_wrapper_emitter.
    @tparam C a reflection of the vector specialization.
    @param doc the growing document. */
template <std::meta::info C>
void ensure_vector(document& doc) {
    vector_wrapper_emitter{doc}.ensure<C>();
}

} // namespace welder::inline v0::rods::csharp
