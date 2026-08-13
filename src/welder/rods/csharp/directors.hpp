#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <vector>

#include <welder/bind_traits.hpp>
#include <welder/reflect.hpp>                 // welded_for
#include <welder/rods/csharp/type_map.hpp>    // classify / spellings / lookup
#include <welder/virtuals.hpp>             // the neutral slot machinery

/** @file
    The **director** layer of the C# rod: C# subclasses overriding C++ virtuals.

    C# cannot patch a C++ vtable, so the generated shim defines — per welded
    virtual type `T` — a C++ **director subclass** whose overrides call through
    a per-class table of C function pointers registered from managed code
    (`[UnmanagedCallersOnly]` static thunks; SWIG's director model). Each
    director instance carries a `GCHandle` context (weak — the wrapper owns the
    C++ object, never the reverse) and an override **bitmask** computed from the
    dynamic C# type, so a slot the C# type does not override falls straight
    through to the qualified base call — including during C++ construction,
    before the context is bound (matching C++'s own in-ctor dispatch).

    The wrapper's virtual-slot methods are `public virtual` and dispatch by
    origin: a **director-constructed** instance takes the qualified base-call
    thunk (C# already resolved the dynamic dispatch — this is also what makes
    `base.Method()` inside an override terminate), while a **C++-originated**
    object takes the ordinary virtual thunk (its dynamic type may be a C++
    subclass the managed side has never heard of).

    Managed exceptions never unwind into C++: the callback traps them into the
    error slot (code 7), the director override rethrows them as
    `shim::managed_exception`, and the next thunk boundary hands them back to
    C# with the original message.

    **Eligibility** (@ref welder::rods::csharp::director_eligible): the type
    has overridable virtual
    slots (the shared `<welder/virtuals.hpp>` set — `welder::bind_flat` opts
    out per type or per method, exactly as for the Python trampolines) and a **virtual destructor** (the wrapper's destroy thunk
    deletes through `T*`). At most 64 slots (the bitmask). A slot whose shape
    cannot cross (C-variadic, reference/pointer class returns, a class-by-value
    return without a copy constructor) is a designed generation-time error
    naming the `bind_flat` escape.
*/

namespace welder::inline v0::rods::csharp {

/** The overridable virtual slots of @a type — one consteval source shared by
    the generator and the generated shim (which re-derives slot @a k with
    @ref director_slot), so the two sides agree by construction. */
consteval std::vector<std::meta::info> director_slots(std::meta::info type) {
    return ::welder::overridable_virtuals(type);
}

/** Slot @a k of @a type (the generated shim's re-derivation entry point). */
consteval std::meta::info director_slot(std::meta::info type, std::size_t k) {
    return director_slots(type)[k];
}

/** Whether @a type has a virtual destructor. */
consteval bool has_virtual_destructor_of(std::meta::info type) {
    for (auto m :
         std::meta::members_of(type, std::meta::access_context::unchecked()))
        if (std::meta::is_destructor(m) && std::meta::is_virtual(m))
            return true;
    return false;
}

/** Whether welded type @a type gets a director (see the file comment). */
consteval bool director_eligible(std::meta::info type) {
    if (::welder::bound_flat(type))
        return false;
    const auto slots{director_slots(type)};
    return !slots.empty() && slots.size() <= 64 &&
           has_virtual_destructor_of(type);
}

/** The index of method @a fn in @a type's slot set (vtable-slot identity, so
    an override matches its base declaration), or `npos` when @a fn is not an
    overridable slot. */
consteval std::size_t director_slot_index(std::meta::info type,
                                          std::meta::info fn) {
    const auto slots{director_slots(type)};
    for (std::size_t i{0}; i < slots.size(); ++i)
        if (::welder::detail::same_slot(slots[i], fn))
            return i;
    return static_cast<std::size_t>(-1);
}

/** Whether slot @a slot's shape can cross the director wire: every parameter
    marshallable, and the return void / scalar / bool / enum / `std::string`
    by value / welded class by value. */
consteval bool director_slot_supported(std::meta::info slot) {
    const auto leaf = [](marshal_kind k) {
        return k == marshal_kind::scalar || k == marshal_kind::boolean ||
               k == marshal_kind::enum_ || k == marshal_kind::utf8_string ||
               k == marshal_kind::handle;
    };
    const std::meta::info R{std::meta::return_type_of(slot)};
    const marshal_kind rk{classify(R)};
    if (rk != marshal_kind::void_ && !leaf(rk))
        return false; // the value-container wires have no director arms yet
    // A POINTER class return crosses as a non-owning view (covariant clone()
    // patterns; the C# override may return null) — the returned object's
    // lifetime is the override's contract, as on the Python rods. References
    // and pointer/reference strings stay out (no null, no length contract).
    if (rk == marshal_kind::handle && type_trait(^^std::is_reference_v, R))
        return false;
    if (rk == marshal_kind::utf8_string &&
        (is_pointer_flavor(R) || type_trait(^^std::is_reference_v, R)))
        return false;
    for (auto p : std::meta::parameters_of(slot))
        if (!leaf(classify(std::meta::type_of(p))))
            return false;
    return true;
}

/** @a type's welded C# ancestor chain (recursive public bases welded for
    `lang::cs`) — the wrapper classes the override-mask check must NOT count
    as user overrides. */
consteval std::vector<std::meta::info> welded_ancestors(std::meta::info type) {
    std::vector<std::meta::info> out{};
    for (auto b : std::meta::bases_of(type,
                                      std::meta::access_context::unchecked())) {
        if (!std::meta::is_public(b))
            continue;
        const std::meta::info bt{std::meta::dealias(std::meta::type_of(b))};
        if (::welder::welded_for(bt, lang::cs))
            out.push_back(bt);
        for (auto a : welded_ancestors(bt))
            out.push_back(a);
    }
    return out;
}

/** The trailing cv/ref/`noexcept` tokens an override of @a fn must repeat
    (each with a leading space) — the trampolines rod's qualifier_tokens,
    duplicated here to keep the C# rod off the Python rod tree's document. */
consteval std::string slot_qualifiers(std::meta::info fn) {
    std::string q{};
    if (std::meta::is_const(fn))
        q += " const";
    if (std::meta::is_volatile(fn))
        q += " volatile";
    if (std::meta::is_lvalue_reference_qualified(fn))
        q += " &";
    if (std::meta::is_rvalue_reference_qualified(fn))
        q += " &&";
    if (std::meta::is_noexcept(fn))
        q += " noexcept";
    return q;
}

} // namespace welder::inline v0::rods::csharp
