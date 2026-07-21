#pragma once
#include <array>
#include <map>
#include <meta>
#include <unordered_map>
#include <vector>

/** @file
    The **reference-semantic container** table: which STL containers welder can bind
    *opaquely* (by reference) rather than by the frameworks' copy casters.

    By default a `std::vector<T>` / `std::map<K,V>` crosses into Python through
    pybind11's `<pybind11/stl.h>` (nanobind's `<nanobind/stl/…>`) as a **copy** —
    reading it snapshots to a `list`/`dict`, and mutating that snapshot never reaches
    the C++ object. For a dynamic container that is rarely wanted. Both frameworks
    offer the alternative — an **opaque, reference-semantic** binding via
    `bind_vector` / `bind_map` (mutation writes through, `append`/slicing, and — for
    scalar elements — a zero-copy `numpy` view) — which the user opts into by welding a
    **namespace-scope alias** to the specialization (the same mechanism that welds any
    template instantiation), paired with the framework's opaque declaration
    (`PYBIND11_MAKE_OPAQUE` / `NB_MAKE_OPAQUE`, exposed as @c WELDER_OPAQUE by each
    Python rod).

    This header records *which* containers have a ready opaque binder in the
    frameworks — the scope is deliberately exactly that set: `bind_vector` covers
    `std::vector` (pybind11's `bind_vector` needs `.reserve()`/`.data()`, so the
    segmented `std::deque` does not qualify); `bind_map` covers `std::map` /
    `std::unordered_map`. `std::deque`, `std::list`, the sets and the `multi*`
    containers have no cross-framework opaque binder and are *not* listed. The carriage
    consults @ref welder::is_reference_container at the alias-dispatch site to route a
    welded container alias to the rod's `bind_container` hook instead of the generic
    class sweep; the rod reads @ref welder::container_kind_of to pick `bind_vector`
    vs `bind_map`.

    Like `bindable.hpp` this is a reflection-layer header (it names the container
    templates, so it is *not* std-include-free — that constraint is only on the
    vocabulary headers `lang.hpp` / `annotations.hpp`).
*/

namespace welder::inline v0 {

/** Which opaque binder a reference-bound container uses. */
enum class container_kind : unsigned char {
    sequence, /**< A `bind_vector` container: `std::vector`. */
    map,      /**< A `bind_map` container: `std::map`, `std::unordered_map`. */
};

namespace detail {

/** One row of the reference-container table: a container template paired with the
    opaque binder kind it uses. @see welder::container_kind */
struct container_spec {
    std::meta::info tmpl;  /**< The class template, e.g. `^^std::vector`. */
    container_kind kind;   /**< Which opaque binder registers it. */
};

/** The containers welder binds opaquely — exactly those the frameworks' `bind_vector`
    / `bind_map` support. Add a row only when both Python frameworks ship a binder for
    it (a deque/set/list/multimap would need welder to *synthesize* the Python
    protocol — pybind11's `bind_vector` alone already rejects `std::deque`, which lacks
    `.reserve()`). */
consteval std::array<container_spec, 3> reference_containers() {
    return {{
        {^^std::vector, container_kind::sequence},
        {^^std::map, container_kind::map},
        {^^std::unordered_map, container_kind::map},
    }};
}

} // namespace detail

/** Is @a type one of the containers welder can bind by reference (opaquely)?

    @param type a reflection of the (possibly aliased — dealias first) type.
    @return `true` iff @a type is a specialization of a listed container template. */
consteval bool is_reference_container(std::meta::info type) {
    if (!std::meta::has_template_arguments(type))
        return false;
    const std::meta::info tmpl{std::meta::template_of(type)};
    for (const detail::container_spec& c : detail::reference_containers())
        if (c.tmpl == tmpl)
            return true;
    return false;
}

/** The opaque-binder kind of @a type.
    @param type a reflection of the container specialization.
    @pre @ref is_reference_container(@a type) is `true`.
    @return `container_kind::sequence` for `bind_vector` containers, `map` for
            `bind_map` containers. */
consteval container_kind container_kind_of(std::meta::info type) {
    const std::meta::info tmpl{std::meta::template_of(type)};
    for (const detail::container_spec& c : detail::reference_containers())
        if (c.tmpl == tmpl)
            return c.kind;
    return container_kind::sequence; // unreachable given the precondition
}

/** Is @a type a **contiguous** sequence — one whose elements live in a single block
    reachable via `.data()`, so a scalar element type can be exposed zero-copy through
    the buffer protocol / an `nb::ndarray` view?

    Among the reference containers only `std::vector` qualifies; the maps are not
    sequences. The buffer/ndarray path is gated on this **and** an arithmetic
    (non-`bool`) element type. (Kept as its own predicate — rather than folding into
    `container_kind` — so a future segmented/opaque sequence stays bindable by
    reference while correctly getting no buffer view.)
    @param type a reflection of the container specialization. */
consteval bool container_is_contiguous(std::meta::info type) {
    return std::meta::has_template_arguments(type) &&
           std::meta::template_of(type) == ^^std::vector;
}

/** Does rod @a B implement the optional `bind_container` hook (i.e. can it bind
    STL containers opaquely)? Only the Python rods do — the Lua runtimes already
    give containers structural reference semantics, so they never define it and a
    welded container alias is diagnosed for them. Detected by shape, exactly as the
    other optional rod hooks are (a requires-expression, so no body is instantiated).
    @tparam B the rod. */
template <class B>
concept rod_binds_containers =
    requires(typename B::module_type& m, const char* s) {
        B::template bind_container<std::vector<int>>(m, s);
    };

} // namespace welder
