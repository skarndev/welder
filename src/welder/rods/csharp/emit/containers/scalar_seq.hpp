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
    @ref welder::rods::csharp::scalar_seq_wrapper_emitter is the component.

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

/** The component that generates the reference-semantic wrapper for a
    SCALAR/ENUM sequence used as a live field (`std::vector<int>` /
    `std::array<double, 3>` members): Count, a bounds-checked indexer,
    `Add`/`Clear` (vector only), bulk `CopyFrom`, an implicit conversion from
    `T[]` (so whole-property assignment reads naturally), and — the zero-copy
    path — `AsSpan()` over the C++ buffer (`Span<T>` IS C#'s buffer protocol;
    valid until a size-changing operation or Dispose, exactly a C++ iterator's
    rule).

    @ref ensure derives the element spellings (C#, wire, symbol token) and the
    fixed/vector shape, then writes the three artifacts as named steps — the
    vector-only ops (`_size`/`_push`/`_clear`) branch inside each step. */
class scalar_seq_wrapper_emitter {
  public:
    /** Bind the emitter to the growing document.
        @param doc the two-artifact document. */
    explicit scalar_seq_wrapper_emitter(document& doc) : doc_{&doc} {}

    /** Generate the wrapper for sequence specialization @a C, if this is the
        first time it is seen (keyed by the display string): the rename
        registration, the op symbols, and the three artifacts.
        @tparam C a reflection of the `std::vector`/`std::array`
                  specialization (scalar or enum element). */
    template <std::meta::info C>
    void ensure() {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!doc_->claim_container(key))
            return;
        constexpr std::meta::info E{
            std::meta::dealias(sequence_element(C))};
        constexpr bool fixed{is_fixed_sequence(C)};
        constexpr bool enum_elem{classify(E) == marshal_kind::enum_};
        fixed_ = fixed;
        enum_elem_ = enum_elem;
        // the element's C# spelling / name token / wire spelling
        std::string ename{};
        if constexpr (enum_elem) {
            ecs_ = type_ref<bare(E)>();
            ename = field_ref<bare(E)>();
        } else {
            constexpr const char* c{scalar_spell(E).cs};
            ecs_ = c;
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
        wire_ = wire;
        wire_cs_ = wire_cs;
        if constexpr (fixed)
            ns_ = std::to_string(fixed_extent(C));
        sym_ = fixed ? "welder_arrs_" + std::string{tok} + "_" + ns_
                     : "welder_vecs_" + std::string{tok};
        targs_ = fixed ? "^^" + std::string{ecpp} + ", " + ns_
                       : "^^" + std::string{ecpp};
        V_ = fixed ? "Array" + ename + "x" + ns_ : "Vector" + ename;
        doc_->record_type_name(key, V_);
        pfx_ = fixed ? "arr" : "vec";
        emit_thunks();
        emit_pinvokes();
        emit_wrapper();
    }

  private:
    /** Write the native op thunks (registering their symbols in step): the
        shared `_new`/`_destroy`/`_data`/`_fill` quartet, then — vector only —
        `_size`/`_push`/`_clear`. */
    void emit_thunks() {
        for (const char* leaf : {"_new", "_destroy", "_data", "_fill"})
            doc_->record_symbol(sym_ + leaf);
        code_writer t{doc_->shim, 0};
        t.line("void* {}_new(welder_error* err) { return wcs::shim::{}"
               "_new<{}>(err); }",
               sym_, pfx_, targs_);
        t.blank();
        t.line("void {}_destroy(void* self) { wcs::shim::{}_destroy<{}>"
               "(self); }",
               sym_, pfx_, targs_);
        t.blank();
        t.line("void* {}_data(void* self, welder_error* err) { return "
               "wcs::shim::{}_data<{}>(self, err); }",
               sym_, pfx_, targs_);
        t.blank();
        t.line("void {}_fill(void* self, const void* data, std::int64_t "
               "len, welder_error* err) { wcs::shim::{}_fill<{}>"
               "(self, data, len, err); }",
               sym_, pfx_, targs_);
        t.blank();
        if (!fixed_) {
            for (const char* leaf : {"_size", "_push", "_clear"})
                doc_->record_symbol(sym_ + leaf);
            t.line("std::int64_t {}_size(void* self, welder_error* err) "
                   "{ return wcs::shim::vec_size<{}>(self, err); }",
                   sym_, targs_);
            t.blank();
            t.line("void {}_push(void* self, {} v, welder_error* err) { "
                   "wcs::shim::vec_push<{}>(self, v, err); }",
                   sym_, wire_, targs_);
            t.blank();
            t.line("void {}_clear(void* self, welder_error* err) { "
                   "wcs::shim::vec_clear<{}>(self, err); }",
                   sym_, targs_);
            t.blank();
        }
    }

    /** Write the `[LibraryImport]` declarations for the op thunks (the
        vector-only trio branches like the thunks). */
    void emit_pinvokes() {
        code_writer p{doc_->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_new(out WelderError err);",
               sym_);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_destroy(IntPtr self);",
               sym_);
        p.line("[LibraryImport(Lib)] internal static partial IntPtr "
               "{}_data({}Handle self, out WelderError err);",
               sym_, V_);
        p.line("[LibraryImport(Lib)] internal static partial void {}"
               "_fill({}Handle self, IntPtr data, long len, out "
               "WelderError err);",
               sym_, V_);
        if (!fixed_) {
            p.line("[LibraryImport(Lib)] internal static partial long "
                   "{}_size({}Handle self, out WelderError err);",
                   sym_, V_);
            p.line("[LibraryImport(Lib)] internal static partial void "
                   "{}_push({}Handle self, {} v, out WelderError err);",
                   sym_, V_, wire_cs_);
            p.line("[LibraryImport(Lib)] internal static partial void "
                   "{}_clear({}Handle self, out WelderError err);",
                   sym_, V_);
        }
    }

    /** Write the managed side: the `SafeHandle` owning the native sequence
        and the public wrapper class — `Count` (constant when fixed),
        `AsSpan()`, the span-backed indexer, `Add`/`Clear` (vector only),
        `ToArray`, `CopyFrom`, the implicit `T[]` conversion, `Dispose`. */
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
        w.line("/// <summary>A reference-semantic C++ {}{} (live element "
               "access; AsSpan() is a zero-copy view over the C++ buffer, "
               "valid until a size-changing operation or Dispose).</summary>",
               fixed_ ? "std::array of " + ns_ + " " : std::string{"vector of "},
               ecs_);
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
            if (fixed_)
                w.line("public int Count => {};", ns_);
            else {
                w.line("public int Count");
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
            w.line("public unsafe Span<{}> AsSpan()", ecs_);
            {
                const auto body{w.braces()};
                w.line("IntPtr _d = NativeMethods.{}_data(_h_{}, out "
                       "WelderError _e);",
                       sym_, V_);
                w.line("WelderInterop.ThrowIfError(in _e);");
                w.line("var _s = new Span<{}>((void*)_d, Count);", ecs_);
                w.line("GC.KeepAlive(this);");
                w.line("return _s;");
            }
            w.line("public {} this[int i]", ecs_);
            {
                const auto prop{w.braces()};
                w.line("get => AsSpan()[i];");
                w.line("set => AsSpan()[i] = value;");
            }
            if (!fixed_) {
                w.line("public void Add({} item)", ecs_);
                {
                    const auto body{w.braces()};
                    w.line("NativeMethods.{}_push(_h_{}, {}, out "
                           "WelderError _e);",
                           sym_, V_,
                           enum_elem_ ? "(" + wire_cs_ + ")item"
                                      : std::string{"item"});
                    w.line("WelderInterop.ThrowIfError(in _e);");
                }
                w.line("public void Clear()");
                {
                    const auto body{w.braces()};
                    w.line("NativeMethods.{}_clear(_h_{}, out "
                           "WelderError _e);",
                           sym_, V_);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                }
            }
            w.line("public {}[] ToArray() => AsSpan().ToArray();", ecs_);
            w.line("public unsafe void CopyFrom(ReadOnlySpan<{}> src)", ecs_);
            {
                const auto body{w.braces()};
                w.line("fixed ({}* _p = src)", ecs_);
                {
                    const auto pin{w.braces()};
                    w.line("NativeMethods.{}_fill(_h_{}, (IntPtr)_p, "
                           "src.Length, out WelderError _e);",
                           sym_, V_);
                    w.line("WelderInterop.ThrowIfError(in _e);");
                }
            }
            w.line("public static implicit operator {}({}[] a)", V_, ecs_);
            {
                const auto body{w.braces()};
                w.line("var _v = new {}();", V_);
                w.line("_v.CopyFrom(a);");
                w.line("return _v;");
            }
            w.line("public void Dispose() => _h_{}.Dispose();", V_);
        }
        w.blank();
    }

    document* doc_;       /**< The shared document. */
    bool fixed_{false};   /**< `std::array` (fixed) vs `std::vector`. */
    bool enum_elem_{false}; /**< Whether the element is an enum (push casts). */
    std::string ecs_{};   /**< The element's C# spelling. */
    std::string ns_{};    /**< Fixed form: the extent `N`, as text. */
    std::string sym_{};   /**< The C symbol stem (`welder_vecs_…`/`welder_arrs_…`). */
    std::string targs_{}; /**< The shim's template arguments. */
    std::string V_{};     /**< The wrapper class's name. */
    std::string wire_{};  /**< The element's C-ABI wire spelling. */
    std::string wire_cs_{}; /**< The element's C# wire spelling. */
    std::string pfx_{};   /**< The shim family prefix (`vec`/`arr`). */
};

/** The reference-semantic wrapper for a SCALAR/ENUM sequence used as a
    live field (`std::vector<int>` / `std::array<double, 3>` members):
    Count, a bounds-checked indexer, `Add`/`Clear` (vector only), bulk
    `CopyFrom`, an implicit conversion from `T[]` (so whole-property
    assignment reads naturally), and — the zero-copy path — `AsSpan()`
    over the C++ buffer (`Span<T>` IS C#'s buffer protocol; valid until a
    size-changing operation or Dispose, exactly a C++ iterator's rule).
    Forwards into @ref welder::rods::csharp::scalar_seq_wrapper_emitter.
    @tparam C a reflection of the sequence specialization.
    @param doc the growing document. */
template <std::meta::info C>
void ensure_scalar_seq(document& doc) {
    scalar_seq_wrapper_emitter{doc}.ensure<C>();
}

} // namespace welder::inline v0::rods::csharp
