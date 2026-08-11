#pragma once
#include <cstddef>
#include <filesystem>
#include <meta>
#include <string>
#include <string_view>

#include <welder/carriage.hpp> // marker_resolution::counts_as_registered (classify's
                               // handle test IS the gate's registration oracle)
#include <welder/diag.hpp>     // csharp_unmarshallable
#include <welder/rods/csharp/marshal/families.hpp>

/** @file
    **How a C++ type crosses the C ABI**: the @ref welder::rods::csharp::marshal_kind
    taxonomy, the @ref welder::rods::csharp::classify function that assigns one,
    and the @ref welder::rods::csharp::require_marshallable gate that turns
    "not representable" into a designed generation-time error.

    `classify` is the single decision point of the whole backend: every wire
    spelling, every managed spelling, every shim conversion and every generated
    wrapper branches on its result, in the generator and in the compiled
    marshalling library alike. Adding a type family therefore means adding a kind
    here and then following the compiler to the (few) places that switch on it.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

/** How a value type crosses the C ABI boundary. */
enum class marshal_kind {
    void_,       /**< `void` (return only). */
    scalar,      /**< An arithmetic type passed by value. */
    boolean,     /**< `bool` — one byte, `[MarshalAs(U1)]` on the managed side. */
    utf8_string, /**< `std::string` / `std::string_view` / `char*` /
                      `std::filesystem::path` — UTF-8. */
    enum_,       /**< A welded enum: crosses as its underlying value (`enum : <u>`). */
    handle,      /**< A welded class: crosses as an opaque `void*`/`IntPtr`. */
    optional_,   /**< `std::optional` of a leaf kind: crosses by value as the
                      fixed `welder_opt_wire` struct ⇄ C# `T?`. */
    seq_value,   /**< `std::vector`/`std::array` of scalar/enum elements:
                      crosses by VALUE (a copy) as `welder_seq_wire` ⇄ `T[]`. */
    seq_ref,     /**< `std::vector` of a WELDED class: crosses as an opaque
                      handle behind a generated reference-semantic C# wrapper
                      (live element views — welder's opaque-container model). */
    tuple_value, /**< `std::pair`/`std::tuple` of LEAF elements: crosses by
                      value as an array of `welder_opt_wire` slots ⇄ a C#
                      ValueTuple. */
    map_ref,     /**< `std::map`/`std::unordered_map` with a leaf key: an
                      opaque handle behind a generated reference-semantic C#
                      wrapper (live mapped views for welded values). */
    shared_ptr_, /**< `std::shared_ptr<welded>`: params borrow (a non-owning
                      aliasing shared_ptr); returns share (the wrapper's view
                      is pinned by a boxed shared_ptr copy). */
    unique_ptr_, /**< `std::unique_ptr<welded>` RETURN: ownership transfers
                      (release → an owned wrapper). Params stay unsupported
                      (sink semantics from a GC-owned wrapper are ambiguous). */
    unsupported  /**< Not yet representable — see @ref require_marshallable. */
};

/** Classify how @a type crosses the boundary (see @ref marshal_kind). Enums are
    kept apart from scalars — like every other rod they are welded (the C#
    backend emits a real `enum`), so they are *not* a native scalar here.
    @param type a reflection of the (possibly cv/ref/pointer) type.
    @return its marshalling classification. */
consteval marshal_kind classify(std::meta::info type) {
    namespace m = std::meta;
    // A std::expected crosses as its value type (the error branch throws), so
    // peel before anything else — including the void test, which `Result<void>`
    // must reach.
    type = peel_expected(type);
    if (m::dealias(type) == ^^void)
        return marshal_kind::void_;
    const m::info w{bare(type)};
    if (w == m::dealias(^^std::string) || w == m::dealias(^^std::string_view) ||
        w == m::dealias(^^std::filesystem::path))
        return marshal_kind::utf8_string;
    if (type_trait(^^std::is_pointer_v,
                   m::dealias(m::substitute(^^std::remove_cvref_t, {type})))) {
        // A char* (any cv) is a UTF-8 string; other pointers fall through to
        // their pointee classification below (bare() already peeled one level).
        if (w == ^^char)
            return marshal_kind::utf8_string;
    }
    if (w == ^^bool)
        return marshal_kind::boolean;
    // std::byte is a scoped enum, but it is the standard spelling of "a raw
    // byte", not a program-defined enumeration a rod should mirror: C# already
    // has that type, spelled `byte`. Classifying it as a scalar is what makes
    // `std::vector<std::byte>` (every binary-file API's payload) cross as the
    // `byte[]` a .NET caller expects, instead of demanding it be welded.
    if (w == m::dealias(^^std::byte))
        return marshal_kind::scalar;
    if (m::is_enum_type(w))
        return marshal_kind::enum_;
    if (m::is_arithmetic_type(w))
        return marshal_kind::scalar;
    if (m::is_class_type(w)) {
        // The value-marshalled container family (a LEAF payload only — deeper
        // nesting stays unsupported until it earns a wire representation).
        if (is_specialization_of(w, ^^std::optional)) {
            const marshal_kind pk{classify(optional_payload(w))};
            return (pk == marshal_kind::scalar || pk == marshal_kind::boolean ||
                    pk == marshal_kind::enum_ ||
                    pk == marshal_kind::utf8_string ||
                    pk == marshal_kind::handle)
                       ? marshal_kind::optional_
                       : marshal_kind::unsupported;
        }
        if (is_sequence(w)) {
            const marshal_kind ek{classify(sequence_element(w))};
            // NOT bool: std::vector<bool> is a bitset, not contiguous bools.
            if (ek == marshal_kind::scalar || ek == marshal_kind::enum_)
                return marshal_kind::seq_value;
            // A welded-class element: reference semantics behind a generated
            // wrapper (vector, or the fixed-size array wrapper).
            if (ek == marshal_kind::handle)
                return marshal_kind::seq_ref;
            return marshal_kind::unsupported;
        }
        if (is_specialization_of(w, ^^std::map) ||
            is_specialization_of(w, ^^std::unordered_map)) {
            const marshal_kind kk{classify(map_key_type(w))};
            const marshal_kind vk{classify(map_value_type(w))};
            const bool key_ok{kk == marshal_kind::scalar ||
                              kk == marshal_kind::enum_ ||
                              kk == marshal_kind::utf8_string};
            const bool val_ok{vk == marshal_kind::scalar ||
                              vk == marshal_kind::boolean ||
                              vk == marshal_kind::enum_ ||
                              vk == marshal_kind::utf8_string ||
                              vk == marshal_kind::handle};
            return key_ok && val_ok && is_default_map(w)
                       ? marshal_kind::map_ref
                       : marshal_kind::unsupported;
        }
        if (is_specialization_of(w, ^^std::shared_ptr)) {
            return classify(std::meta::template_arguments_of(w)[0]) ==
                           marshal_kind::handle
                       ? marshal_kind::shared_ptr_
                       : marshal_kind::unsupported;
        }
        if (is_specialization_of(w, ^^std::unique_ptr)) {
            const auto args{std::meta::template_arguments_of(w)};
            return classify(args[0]) == marshal_kind::handle &&
                           std::meta::dealias(std::meta::substitute(
                               ^^std::unique_ptr, {args[0]})) == w
                       ? marshal_kind::unique_ptr_
                       : marshal_kind::unsupported;
        }
        if (is_specialization_of(w, ^^std::pair) ||
            is_specialization_of(w, ^^std::tuple)) {
            const std::size_t n{tuple_arity(w)};
            if (n < 2)
                return marshal_kind::unsupported; // C# has no 1-tuple syntax
            for (std::size_t j{0}; j < n; ++j) {
                const marshal_kind ek{classify(tuple_element_type(w, j))};
                if (ek != marshal_kind::scalar && ek != marshal_kind::boolean &&
                    ek != marshal_kind::enum_ &&
                    ek != marshal_kind::utf8_string &&
                    ek != marshal_kind::handle)
                    return marshal_kind::unsupported;
            }
            return marshal_kind::tuple_value;
        }
        // Every class reaching an emission hook has already passed the
        // bindability gate, whose registration oracle is SCOPE-AWARE
        // (member/namespace aliases, nested chains) in ways a context-free
        // classify cannot replicate — so a non-container class IS a handle.
        // A gate-TRUSTED type that is not actually registered anywhere still
        // cannot fail silently: its name placeholder has no registration to
        // resolve against, so the emitted C# spells the raw C++ name and the
        // first consumer build fails on it, loudly.
        return marshal_kind::handle;
    }
    return marshal_kind::unsupported;
}

/** Whether @a type is one of the two container kinds that cross as a handle
    behind a generated reference-semantic wrapper.
    @param k a marshalling classification.
    @return true for @ref marshal_kind::seq_ref and @ref marshal_kind::map_ref. */
consteval bool is_container_ref(marshal_kind k) {
    return k == marshal_kind::seq_ref || k == marshal_kind::map_ref;
}

/** Whether @a k crosses as an opaque `void*`/`IntPtr` handle — a welded class
    or a generated container wrapper. These share every ownership rule
    (@ref handle_return_of), which is why they share one predicate.
    @param k a marshalling classification.
    @return true for `handle`, `seq_ref` and `map_ref`. */
consteval bool is_handle_like(marshal_kind k) {
    return k == marshal_kind::handle || is_container_ref(k);
}

/** Enforce the current phase's marshalling coverage on a participating
    param/return type: what the shared bindability gate admits but this backend
    cannot yet carry across the C ABI must fail LOUDLY at generation time, never
    silently emit a corrupting `void*`.

    Currently raised here: the structurally-unsupported kinds (containers
    arrive with the container families). Policy-level rejections (`rv::none`,
    `take_ownership` on a reference) live in @ref handle_return_of.
    @param type      the param/return type reflection.
    @param is_return true when @a type is a return type.
    @throws diag::csharp_unmarshallable when the type cannot cross yet. */
consteval void require_marshallable(std::meta::info type, bool is_return) {
    const marshal_kind k{classify(type)};
    if (k == marshal_kind::unsupported)
        throw diag::csharp_unmarshallable{};
    // unique_ptr can only cross OUTWARD (ownership transfer via release);
    // sinking one from a GC-owned wrapper has no sound contract.
    if (!is_return && k == marshal_kind::unique_ptr_)
        throw diag::csharp_unmarshallable{};
    // std::span is INBOUND-ONLY, the mirror image of unique_ptr. As a parameter
    // it is exactly what the seq_value wire already does — the managed array is
    // pinned for the call and the span views it, which is a span's contract. As
    // a RETURN or a FIELD it is a borrow of a buffer the managed side does not
    // own and cannot observe the lifetime of, so it would hand C# a dangling
    // view the moment the C++ owner reallocated or died.
    if (is_return && is_span(type))
        throw diag::csharp_unmarshallable{};
}

} // namespace welder::inline v0::rods::csharp
