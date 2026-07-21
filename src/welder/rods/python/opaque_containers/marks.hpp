#pragma once
#include <meta>

/** @file
    The one opt-out mark of the **opaque-container generator**
    (`welder::rods::opaque_containers::rod`): `welder::rods::python::by_value`.

    The generator binds every reference container (`std::vector`/`std::map`/
    `std::unordered_map`) a welded type uses **by reference** (opaque). Attach
    `by_value` to a data member to keep *its* container type bound the default way —
    by value, copied through the framework's stl caster:
    @code
    struct [[=welder::weld(welder::lang::py)]] Frame {
        std::vector<double> samples;                              // opaque (by reference)
        [[=welder::rods::python::by_value]] std::vector<int> ids; // stays list[int]
    };
    @endcode

    Because opaqueness is a **per-type, module-wide** decision, `by_value` on any use
    site of a container type excludes that whole type from the generator — mirroring
    how the generator's own opt-in is whole-type. It is a *generator-only* mark: the
    runtime binding path never reads it (a container is opaque only if a welded alias
    exists, which the generator omits for a `by_value` type). Kept in its own light
    header so a type header can carry the mark without pulling in the generator rod —
    the same split as `bind_flat` in `<welder/rods/python/trampoline.hpp>`. */

namespace welder::inline v0::rods::python {

/** The stored form of a `by_value` mark (a plain tag — it carries no state). */
struct by_value_spec {};

/** Opt a data member's container type out of the opaque-container generator — it
    keeps default by-value (copy) binding. @see by_value_spec */
inline constexpr by_value_spec by_value{};

/** Does @a entity carry a `by_value` mark? @param entity a reflection of the member. */
consteval bool marked_by_value(std::meta::info entity) {
    return !std::meta::annotations_of_with_type(entity, ^^by_value_spec).empty();
}

} // namespace welder::inline v0::rods::python
