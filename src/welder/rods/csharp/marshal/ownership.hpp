#pragma once
#include <meta>

#include <welder/annotations.hpp> // rv_kind
#include <welder/diag.hpp>        // csharp_unmarshallable
#include <welder/rods/csharp/marshal/classify.hpp>

/** @file
    **Who owns what crosses out**: welder's backend-neutral return-value policy
    (`[[=welder::return_policy]]`, `welder::rv::…`) resolved into the concrete
    thing the C# side must do with a returned handle.

    The resolution happens once, here, and is consumed by BOTH artifacts — the
    generator (which decides the wrapper's `owns` flag, its nullability and
    whether it pins an owner) and the compiled marshalling library (which decides
    between copying, moving, adopting and borrowing) — so the two cannot
    disagree about a lifetime.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

/** How a welded-class RETURN crosses, resolved from its category (value /
    lvalue-reference / pointer) and its `[[=welder::return_policy]]` — decided
    here once, consumed by BOTH the generator (the C# side's `owns` flag,
    nullability and owner-reference) and the shim's marshalling layer, so the
    two sides cannot disagree. */
enum class handle_return {
    copy_owned, /**< Heap-copy into a fresh owned handle (pybind11 `automatic`
                     for values and lvalue refs; `rv::copy`). */
    move_owned, /**< Heap-move into a fresh owned handle (`rv::move`). */
    adopt,      /**< Adopt the returned pointer as owned (`automatic` /
                     `take_ownership` on a pointer). */
    view,       /**< A non-owning view (`rv::reference`; `automatic_reference`
                     on a pointer). */
    view_keepalive, /**< A non-owning view that keeps its parent object alive
                     managed-side (`rv::reference_internal`). */
};

/** Resolve @ref handle_return for return type @a R under policy @a rv.
    @param R  a reflection of the declared return type.
    @param rv the resolved return-value policy.
    @return the ownership treatment both sides implement.
    @throws diag::csharp_unmarshallable for combinations this backend rejects:
    `rv::none` (nanobind-only), and `take_ownership` on an lvalue reference
    (adopting a reference is a double-free trap). */
consteval handle_return handle_return_of(std::meta::info R, rv_kind rv) {
    const bool ptr{is_pointer_flavor(R)};
    switch (rv) {
        case rv_kind::automatic:
            return ptr ? handle_return::adopt : handle_return::copy_owned;
        case rv_kind::automatic_reference:
            return ptr ? handle_return::view : handle_return::copy_owned;
        case rv_kind::take_ownership:
            if (!ptr)
                throw diag::csharp_unmarshallable{};
            return handle_return::adopt;
        case rv_kind::copy:
            return handle_return::copy_owned;
        case rv_kind::move:
            return handle_return::move_owned;
        case rv_kind::reference:
            return handle_return::view;
        case rv_kind::reference_internal:
            return handle_return::view_keepalive;
        default: // rv_kind::none
            throw diag::csharp_unmarshallable{};
    }
}

/** Whether the C# wrapper for this handle return may be `null` (a pointer
    return can carry `nullptr`; values and references cannot).
    @param R a reflection of the declared return type.
    @return true for a pointer-flavor return. */
consteval bool handle_return_nullable(std::meta::info R) {
    return is_pointer_flavor(R);
}

/** The policy a data member's read binds under: a non-const welded-class
    member hands out a live view tied to its parent (the runtime rods'
    `def_readwrite` reference_internal semantics); everything else crosses by
    value. Mirrored structurally by `shim::field_get`.
    @param MT a reflection of the member's declared type.
    @return the return-value policy its getter binds under. */
consteval ::welder::rv_kind field_return_policy(std::meta::info MT) {
    return (is_handle_like(classify(MT)) && !std::meta::is_const_type(MT))
               ? ::welder::rv_kind::reference_internal
               : ::welder::rv_kind::automatic;
}

} // namespace welder::inline v0::rods::csharp
