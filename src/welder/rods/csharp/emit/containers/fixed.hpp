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
    explicit fixed_wrapper_emitter(document& doc) : doc_{&doc} {}

    /** Generate the wrapper for array specialization @a C, if this is the
        first time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the `std::array` specialization. */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!doc_->claim_container(key))
            return;
        constexpr std::size_t n{fixed_extent(C)};
        ns_ = std::to_string(n);
        constexpr std::meta::info El{
            std::meta::remove_cvref(sequence_element(C))};
        constexpr bool welded_elem{classify(El) == marshal_kind::handle};
        ensure_element_wrapper<El>(*doc_);
        sym_ = std::string{"welder_arr"} + ns_ + "_" + symtok_v<El>;
        targs_ = "^^" + element_cpp_spelling<El>() + ", " + ns_;
        V_ = container_ref<C>();
        E_ = welded_elem ? type_ref<El>() : container_ref<El>();
        Ef_ = welded_elem ? field_ref<El>() : container_ref<El>();
        doc_->record_type_name(key, "Array" + Ef_ + "x" + ns_);
        for (const char* leaf : {"_new", "_destroy", "_get", "_set"})
            doc_->record_symbol(sym_ + leaf);
        emit_thunks();
        emit_pinvokes();
        emit_wrapper();
    }

  private:
    /** Write the native op thunks — one-line delegations into the compiled
        `shim::arr_*` support templates, parameterized by element and extent. */
    void emit_thunks() {
        code_writer t{doc_->shim, 0};
        t.line("void* {}_new(welder_error* err) { return "
               "wcs::shim::arr_new<{}>(err); }",
               sym_, targs_);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::arr_destroy<{}>"
               "(self); }",
               sym_, targs_);
        t.blank();
        t.line("void* {}_get(void* self, std::int64_t i, welder_error* "
               "err) { return wcs::shim::arr_get<{}>(self, i, err); }",
               sym_, targs_);
        t.blank();
        t.line("void {}_set(void* self, std::int64_t i, void* elem, "
               "welder_error* err) { wcs::shim::arr_set<{}>"
               "(self, i, elem, err); }",
               sym_, targs_);
        t.blank();
    }

    /** Write the `[LibraryImport]` declarations for the op thunks. */
    void emit_pinvokes() {
        code_writer p{doc_->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_new(out WelderError err);",
               sym_);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_destroy(IntPtr self);",
               sym_);
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_get({}Handle self, long i, out WelderError err);",
               sym_, V_);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_set({}Handle self, long i, {}Handle elem, out WelderError "
               "err);",
               sym_, V_, E_);
    }

    /** Write the managed side: the `SafeHandle` owning the native array and
        the public wrapper class — constant `Count`, a live-view indexer with
        write-through set, `Dispose`. */
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
        w.line("/// <summary>A reference-semantic C++ std::array of {} {} "
               "(live element views; fixed size).</summary>",
               ns_, E_);
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
            w.line("public int Count => {};", ns_);
            w.line("public {} this[int i]", E_);
            {
                const auto prop{w.braces()};
                w.line("get");
                {
                    const auto arm{w.braces()};
                    w.line("IntPtr _r = NativeMethods.{}_get(_h_{}, i, out "
                           "WelderError _e);",
                           sym_, V_);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                    w.line("var _v = new {}(_r, false);", E_);
                    w.line("_v._owner = this;");
                    w.line("return _v;");
                }
                w.line("set");
                {
                    const auto arm{w.braces()};
                    w.line("NativeMethods.{}_set(_h_{}, i, value._h_{}, out "
                           "WelderError _e);",
                           sym_, V_, Ef_);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                }
            }
            w.line("public void Dispose() => _h_{}.Dispose();", V_);
        }
        w.blank();
    }

    document* doc_;     /**< The shared document. */
    std::string ns_{};  /**< The extent `N`, as text. */
    std::string sym_{}; /**< The C symbol stem (`welder_arr<N>_<eltok>`). */
    std::string targs_{}; /**< The shim's template arguments (`^^element, N`). */
    std::string V_{};   /**< The wrapper class's name reference. */
    std::string E_{};   /**< The element's C# reference (view type). */
    std::string Ef_{};  /**< The element's identifier-safe (field) form. */
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
