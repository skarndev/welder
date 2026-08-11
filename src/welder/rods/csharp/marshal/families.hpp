#pragma once
#include <array>        // ^^std::array (the value-sequence family)
#include <cstddef>
#include <expected>     // ^^std::expected (the fallible-result family)
#include <map>          // ^^std::map (the reference-map family)
#include <memory>       // ^^std::shared_ptr / ^^std::unique_ptr
#include <meta>
#include <optional>     // ^^std::optional
#include <span>         // ^^std::span (the inbound-only sequence)
#include <tuple>        // ^^std::tuple
#include <type_traits>
#include <unordered_map>
#include <utility>      // ^^std::pair
#include <vector>

/** @file
    The standard-library **type families** the C# backend recognises, as
    consteval predicates and accessors over reflections.

    Everything here is purely structural — "is this a `std::optional`, and what
    is its payload?" — with no opinion on how the family crosses the C ABI. That
    opinion lives one layer up, in `<welder/rods/csharp/marshal/classify.hpp>`,
    which composes these predicates into a @ref welder::rods::csharp::marshal_kind.

    Two of them do more than ask a question:

    - @ref welder::rods::csharp::peel_expected removes a `std::expected` wrapper,
      because that family is a *calling convention* (throw the error branch), not
      a wire shape;
    - @ref welder::rods::csharp::bare strips cv, references and one level of
      pointer — the "what type is this really" reduction every downstream
      spelling starts from.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

/** Is @a type (dealias'd) a specialization of class template @a tmpl?
    @param type a reflection of the type.
    @param tmpl a reflection of the class template.
    @return true when @a type is one of @a tmpl's specializations. */
consteval bool is_specialization_of(std::meta::info type, std::meta::info tmpl) {
    return std::meta::has_template_arguments(type) &&
           std::meta::template_of(type) == tmpl;
}

/** Evaluate a standard unary type-trait variable template on the type @a t.
    @param trait_var a reflection of the trait (e.g. `^^std::is_pointer_v`).
    @param t         a reflection of the type to test.
    @return the trait's value. */
consteval bool type_trait(std::meta::info trait_var, std::meta::info t) {
    return std::meta::extract<bool>(std::meta::substitute(trait_var, {t}));
}

// --- std::expected ----------------------------------------------------------

/** Is @a type (after cv/ref stripping) a `std::expected` — the fallible-result
    family that crosses as its VALUE type, the error branch becoming a thrown
    exception?
    @param type a reflection of the (possibly cv/ref) type.
    @return true for a `std::expected` specialization. */
consteval bool is_expected(std::meta::info type) {
    namespace m = std::meta;
    return is_specialization_of(
        m::dealias(m::substitute(^^std::remove_cvref_t, {type})), ^^std::expected);
}

/** The value type of the `std::expected` @a type — its first template argument.
    @param type a reflection of the (possibly cv/ref) expected.
    @return the value type. */
consteval std::meta::info expected_value_type(std::meta::info type) {
    namespace m = std::meta;
    return m::template_arguments_of(
        m::dealias(m::substitute(^^std::remove_cvref_t, {type})))[0];
}

/** The error type of the `std::expected` @a type — its second template argument.
    @param type a reflection of the (possibly cv/ref) expected.
    @return the error type. */
consteval std::meta::info expected_error_type(std::meta::info type) {
    namespace m = std::meta;
    return m::template_arguments_of(
        m::dealias(m::substitute(^^std::remove_cvref_t, {type})))[1];
}

/** Peel a `std::expected` down to the type that actually crosses the boundary.

    `std::expected<T, E>` is not a *shape* on the wire — it is a **calling
    convention**: the value branch crosses as `T`, the error branch is thrown and
    surfaces managed-side as an exception (the wire's `welder_error` slot, which
    every thunk already carries). Peeling here — in `bare`, hence in `classify`
    and therefore in every downstream spelling — is what makes a
    `std::expected<Foo, Err>`-returning method look exactly like a `Foo`-returning
    one to the whole generator; only the shim's return marshalling
    (`shim::guarded`) knows the difference, because only it has to unwrap.
    Recursive, so a doubly-wrapped expected still lands on its payload.
    @param type a reflection of the (possibly cv/ref) type.
    @return the peeled value type, or @a type unchanged when it is not expected. */
consteval std::meta::info peel_expected(std::meta::info type) {
    std::meta::info w{type};
    while (is_expected(w))
        w = expected_value_type(w);
    return w;
}

/** The cv/ref/pointer-stripped bare type of @a type — with any `std::expected`
    wrapper peeled first (@ref peel_expected).
    @param type a reflection of the declared type.
    @return the reduced type every spelling layer works from. */
consteval std::meta::info bare(std::meta::info type) {
    namespace m = std::meta;
    type = peel_expected(type);
    m::info w{m::dealias(m::substitute(^^std::remove_cvref_t, {type}))};
    if (type_trait(^^std::is_pointer_v, w))
        w = m::dealias(m::substitute(
            ^^std::remove_cv_t, {m::substitute(^^std::remove_pointer_t, {w})}));
    return w;
}

/** Whether @a type is a pointer (possibly behind cv/ref) — used to split the
    handle kind's pointer flavor from value/reference.
    @param type a reflection of the declared type.
    @return true for a (possibly cv/ref-qualified) pointer. */
consteval bool is_pointer_flavor(std::meta::info type) {
    namespace m = std::meta;
    return type_trait(^^std::is_pointer_v,
                      m::dealias(m::substitute(^^std::remove_cvref_t, {type})));
}

// --- maps -------------------------------------------------------------------

/** A map's key type (its first template argument).
    @param type a reflection of the map specialization.
    @return the key type. */
consteval std::meta::info map_key_type(std::meta::info type) {
    return std::meta::template_arguments_of(type)[0];
}

/** A map's mapped type (its second template argument).
    @param type a reflection of the map specialization.
    @return the mapped type. */
consteval std::meta::info map_value_type(std::meta::info type) {
    return std::meta::template_arguments_of(type)[1];
}

/** Whether @a type is the DEFAULT-ARGUMENT form of its map template (a custom
    comparator/hasher/allocator would make the re-derived spelling a different
    type — those stay unsupported).
    @param type a reflection of a `std::map` / `std::unordered_map` specialization.
    @return true when only the key and mapped types were supplied. */
consteval bool is_default_map(std::meta::info type) {
    const std::meta::info k{map_key_type(type)};
    const std::meta::info v{map_value_type(type)};
    if (is_specialization_of(type, ^^std::map))
        return std::meta::dealias(std::meta::substitute(^^std::map, {k, v})) ==
               type;
    return std::meta::dealias(std::meta::substitute(^^std::unordered_map,
                                                    {k, v})) == type;
}

// --- pair / tuple -----------------------------------------------------------

/** `std::pair`/`std::tuple`'s @a j-th element type (all arguments are
    elements for both).
    @param type a reflection of the pair/tuple specialization.
    @param j    the element index.
    @return the element type. */
consteval std::meta::info tuple_element_type(std::meta::info type,
                                             std::size_t j) {
    return std::meta::template_arguments_of(type)[j];
}

/** The pair/tuple arity.
    @param type a reflection of the pair/tuple specialization.
    @return the number of elements. */
consteval std::size_t tuple_arity(std::meta::info type) {
    return std::meta::template_arguments_of(type).size();
}

// --- optional / sequences ---------------------------------------------------

/** `std::optional<P>`'s payload type.
    @param type a reflection of the optional specialization.
    @return the payload type `P`. */
consteval std::meta::info optional_payload(std::meta::info type) {
    return std::meta::template_arguments_of(type)[0];
}

/** A `std::vector`/`std::array`/`std::span` sequence's element type.
    @param type a reflection of the sequence specialization.
    @return the element type. */
consteval std::meta::info sequence_element(std::meta::info type) {
    return std::meta::template_arguments_of(type)[0];
}

/** Whether @a type is the fixed-size sequence (`std::array`).
    @param type a reflection of the (bare) sequence type.
    @return true for a `std::array` specialization. */
consteval bool is_fixed_sequence(std::meta::info type) {
    return is_specialization_of(type, ^^std::array);
}

/** Whether @a type (cv/ref stripped) is a `std::span` — the non-owning sequence,
    admitted in parameter position only.
    @param type a reflection of the (possibly cv/ref) type.
    @return true for a `std::span` specialization.
    @see require_marshallable */
consteval bool is_span(std::meta::info type) {
    return is_specialization_of(
        std::meta::dealias(std::meta::substitute(^^std::remove_cvref_t, {type})),
        ^^std::span);
}

/** Whether @a type is any of the three sequence spellings the backend reads
    (`std::vector`, `std::array`, `std::span`).
    @param type a reflection of the (bare) type.
    @return true for a sequence specialization. */
consteval bool is_sequence(std::meta::info type) {
    return is_specialization_of(type, ^^std::vector) ||
           is_specialization_of(type, ^^std::array) ||
           is_specialization_of(type, ^^std::span);
}

/** `std::array<T, N>`'s extent N.
    @param type a reflection of the array specialization.
    @return the extent. */
consteval std::size_t fixed_extent(std::meta::info type) {
    return std::meta::extract<std::size_t>(
        std::meta::template_arguments_of(type)[1]);
}

} // namespace welder::inline v0::rods::csharp
