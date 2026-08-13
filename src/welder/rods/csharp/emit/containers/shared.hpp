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
    The per-class **`shared_ptr` box**: a `SafeHandle` whose release frees the
    boxed `std::shared_ptr` copy that pins a shared return's object.
    @ref welder::rods::csharp::shared_box_emitter is the component.

    A `shared_ptr` return crosses as (object pointer, boxed copy). The wrapper
    holds a non-owning view of the object and stores the box as its owner, so the
    C++ object stays alive for exactly as long as the managed view does — and the
    reference count drops when that view is collected or disposed.
*/

namespace welder::inline v0::rods::csharp {

/** The component that generates the per-class shared_ptr BOX: a SafeHandle
    whose release frees the boxed `shared_ptr` copy pinning a shared return's
    object.

    The smallest of the container generators — one free thunk, one P/Invoke,
    and the `SafeHandle` subclass (there is no public wrapper class; the box
    rides as the view's `_owner`). */
class shared_box_emitter {
  public:
    /** Bind the emitter to the growing document.
        @param doc the two-artifact document. */
    explicit shared_box_emitter(document& doc) : doc_{&doc} {}

    /** Generate the box for payload class @a T, if this is the first time it
        is seen (keyed by `sp:` + the qualified name).
        @tparam T a reflection of the shared_ptr payload class. */
    template <std::meta::info T>
    void ensure() {
        static constexpr const char* key{std::define_static_string(
            std::string{"sp:"} + qualified_cpp_name(T))};
        if (!doc_->claim_container(key))
            return;
        sym_ = std::string{"welder_sp_"} + symtok_v<T> + "_free";
        // Deferred like the container elements: a shared_ptr payload can be
        // an alias-welded specialization, which has no qualified name to spell
        // here. @see anchor_ref
        cpp_ = anchor_ref<T>();
        box_ = field_ref<T>() + "SharedBox";
        doc_->record_symbol(sym_);
        emit_thunk();
        emit_pinvoke();
        emit_box_class();
    }

  private:
    /** Write the free thunk — a one-line delegation into `shim::sp_free`. */
    void emit_thunk() {
        code_writer t{doc_->shim, 0};
        t.line("void {}(void* box) { wcs::shim::sp_free<^^{}>(box); }", sym_,
               cpp_);
        t.blank();
    }

    /** Write the free thunk's `[LibraryImport]` declaration. */
    void emit_pinvoke() {
        code_writer p{doc_->pinvoke, 2};
        p.line("[LibraryImport(Lib)] internal static partial void "
               "{}(IntPtr box);",
               sym_);
    }

    /** Write the `SafeHandle` subclass whose release frees the boxed
        `shared_ptr` copy. */
    void emit_box_class() {
        code_writer w{doc_->containers, 1};
        w.line("internal sealed class {} : SafeHandle", box_);
        {
            const auto cls{w.braces()};
            w.line("internal {}(IntPtr handle, bool owns) : "
                   "base(IntPtr.Zero, owns)",
                   box_);
            {
                const auto body{w.braces()};
                w.line("SetHandle(handle);");
            }
            w.line("public override bool IsInvalid => handle == IntPtr.Zero;");
            w.line("protected override bool ReleaseHandle()");
            {
                const auto body{w.braces()};
                w.line("NativeMethods.{}(handle);", sym_);
                w.line("return true;");
            }
        }
        w.blank();
    }

    document* doc_;     /**< The shared document. */
    std::string sym_{}; /**< The free thunk's C symbol
                             (`welder_sp_<tok>_free`). */
    std::string cpp_{}; /**< The payload's shim spelling (anchor-deferred). */
    std::string box_{}; /**< The box class's name (`<Type>SharedBox`). */
};

/** The per-class shared_ptr BOX: a SafeHandle whose release frees the
    boxed `shared_ptr` copy pinning a shared return's object. Forwards into
    @ref welder::rods::csharp::shared_box_emitter.
    @tparam T a reflection of the shared_ptr payload class.
    @param doc the growing document. */
template <std::meta::info T>
void ensure_shared(document& doc) {
    shared_box_emitter{doc}.ensure<T>();
}

} // namespace welder::inline v0::rods::csharp
