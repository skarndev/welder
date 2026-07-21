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

namespace detail {
/** The first @a N template arguments of @a Type as a splice-ready `std::array` —
    mirrors `bindable.hpp`'s `leading_args` (a returned array, so no `constexpr`
    holds the `operator new`-backed vector `template_arguments_of` yields). */
template <std::meta::info Type, std::size_t N>
consteval std::array<std::meta::info, N> value_args() {
    std::array<std::meta::info, N> out{};
    std::size_t i{0};
    for (std::meta::info a : std::meta::template_arguments_of(Type)) {
        if (i == N)
            break;
        out[i++] = a;
    }
    return out;
}

/** Do all the value args in @a Args convert natively under @a B? */
template <class B, auto Args, std::size_t... I>
consteval bool args_native(std::index_sequence<I...>) {
    return (B::template has_native_caster<typename [:Args[I]:]> && ...);
}
} // namespace detail

/** Are @a Type's element/key/value types all **native** to rod @a B (scalars,
    strings — nothing that needs its own class registration)?

    Drives the carriage's *container-first* pre-pass: a container welded before the
    classes that use it must have its element types already registerable-free,
    otherwise binding it ahead of a welded-class element would make the framework
    spell that element's raw C++ name in the container's method docstrings/stubs (a
    def-time-ordering artifact). Native-element containers (`std::vector<int>`,
    `std::map<std::string,int>`) have no such element, so they bind first safely;
    a `std::vector<Welded>` instead binds in declaration order, after its element.
    @tparam B    the rod (a @ref caster_oracle).
    @tparam Type the container specialization. @pre @ref is_reference_container. */
template <class B, std::meta::info Type>
consteval bool container_elements_native() {
    constexpr std::size_t n{container_kind_of(Type) == container_kind::map ? 2u
                                                                           : 1u};
    constexpr auto args{detail::value_args<Type, n>()};
    return detail::args_native<B, args>(std::make_index_sequence<n>{});
}

} // namespace welder
