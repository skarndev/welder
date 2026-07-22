#pragma once
#include <array>
#include <cstddef>
#include <meta>
#include <string>
#include <type_traits>
#include <utility>

/** @file
    The NumPy **array-interface** descriptor for an opaque `std::vector<T>` whose
    element `T` is a plain-old-data struct of arithmetic fields.

    A contiguous `std::vector<T>` is a block of a fixed layout, so NumPy can view its
    `data()` zero-copy. For a *scalar* `T` the Python rods expose the buffer protocol;
    for a **POD struct** `T` (`Vec3 { float x, y, z; }`, `Particle { double pos[3];
    float mass; }`) they instead expose the [NumPy array-interface protocol]
    (https://numpy.org/doc/stable/reference/arrays.interface.html) — a plain
    `__array_interface__` dict `numpy.asarray` reads. That gives a **structured** array
    (named fields), is **zero-copy** and **writable**, works identically on both Python
    rods (it is just a Python attribute), and needs **no NumPy at build or import time**
    (unlike pybind11's `PYBIND11_NUMPY_DTYPE`, which pulls in `<pybind11/numpy.h>` and
    does not even compile under gcc-16 reflection).

    This header supplies the backend-neutral pieces the rods build the dict from: the
    C++-arithmetic-to-NumPy `typestr` map, the field `descr` (with padding), the whole
    itemsize `typestr`, and the eligibility predicate. Only std::meta / std here — no
    welder vocabulary — so it is a light include for both Python rods. */

namespace welder::inline v0::rods::python {

/** Decimal render of @a n (constexpr `std::to_string` is unavailable on gcc-16). */
consteval std::string ai_uint_string(std::size_t n) {
    if (n == 0)
        return "0";
    std::string s{};
    for (; n; n /= 10)
        s.insert(s.begin(), char('0' + n % 10));
    return s;
}

/** Is @a t one of the unsigned integer fundamental types? (std::meta has no
    `is_signed`, so the unsigned set is enumerated.) */
consteval bool ai_is_unsigned(std::meta::info t) {
    t = std::meta::dealias(t);
    for (std::meta::info u :
         {^^unsigned char, ^^unsigned short, ^^unsigned int, ^^unsigned long,
          ^^unsigned long long, ^^char8_t, ^^char16_t, ^^char32_t})
        if (t == u)
            return true;
    return false;
}

/** The NumPy `typestr` for arithmetic type @a t (`'<'` little-endian, `'|'` for a
    single byte), or `""` if @a t has no portable NumPy scalar type (a non-arithmetic
    type, or `long double`).

    `float`→`"<f4"`, `double`→`"<f8"`, `bool`→`"|b1"`, signed/unsigned integers by
    size — `int`→`"<i4"`, `unsigned char`→`"|u1"`, `std::int64_t`→`"<i8"`, … */
consteval std::string numpy_typestr(std::meta::info t) {
    t = std::meta::dealias(t);
    if (t == ^^bool)
        return "|b1";
    if (t == ^^float)
        return "<f4";
    if (t == ^^double)
        return "<f8";
    if (!std::meta::is_arithmetic_type(t) || t == ^^long double)
        return ""; // no portable NumPy scalar
    const std::size_t sz{std::meta::size_of(t)};
    const char code{ai_is_unsigned(t) ? 'u' : 'i'};
    const char prefix{sz == 1 ? '|' : '<'};
    return std::string{prefix} + code + ai_uint_string(sz);
}

/** The array-interface `typestr` for the whole element @a E — an opaque `|V<sizeof>`
    void field; the per-field @ref ai_descr carries the meaning. */
template <class E>
consteval const char* ai_typestr() {
    return std::define_static_string("|V" + ai_uint_string(sizeof(E)));
}

/** Whether every one of a POD struct's fields maps to a NumPy scalar (so the descr is
    complete). A field-less struct or one with a non-arithmetic field is ineligible. */
consteval bool ai_all_fields_numpy(std::meta::info E) {
    const auto members{std::meta::nonstatic_data_members_of(
        E, std::meta::access_context::unchecked())};
    if (members.empty())
        return false;
    for (std::meta::info m : members)
        if (numpy_typestr(std::meta::type_of(m)).empty())
            return false;
    return true;
}

/** Is `std::vector<E>` viewable as a NumPy **structured** array — i.e. is @a E a
    trivially-copyable, standard-layout class whose fields are all NumPy scalars?
    Excludes scalars (they take the buffer-protocol path) and, by construction, any
    vtable'd / string-holding / pointer-holding type (not trivially copyable). */
template <class E>
consteval bool pod_array_eligible() {
    return std::is_class_v<E> && std::is_trivially_copyable_v<E> &&
           std::is_standard_layout_v<E> && ai_all_fields_numpy(^^E);
}

/** The number of array-interface `descr` entries for @a E — one per field, plus one
    per padding gap (interior or trailing). @see ai_descr */
consteval std::size_t ai_entry_count(std::meta::info E) {
    std::size_t n{0}, running{0};
    for (std::meta::info m : std::meta::nonstatic_data_members_of(
             E, std::meta::access_context::unchecked())) {
        // offset_of(m).bytes is std::ptrdiff_t (signed); a field offset is
        // non-negative, so cast to size_t (avoids -Werror=narrowing/sign-conversion).
        const std::size_t off{
            static_cast<std::size_t>(std::meta::offset_of(m).bytes)};
        if (off > running)
            ++n; // interior padding
        ++n;     // the field
        running = off + std::meta::size_of(std::meta::type_of(m));
    }
    if (running < std::meta::size_of(E))
        ++n; // trailing padding
    return n;
}

/** The NumPy array-interface `descr` for @a E: a splice-ready `std::array` of
    `(name, typestr)` pairs — each field in declaration order, with `("", "|V<gap>")`
    void entries for interior and trailing padding so the itemsize and field offsets
    match `E`'s layout exactly. The rods materialize this into the `descr` list at
    runtime (the `const char*`s are static, so the array iterates as ordinary data). */
template <std::meta::info E>
consteval std::array<std::pair<const char*, const char*>, ai_entry_count(E)>
ai_descr() {
    std::array<std::pair<const char*, const char*>, ai_entry_count(E)> out{};
    std::size_t i{0}, running{0};
    for (std::meta::info m : std::meta::nonstatic_data_members_of(
             E, std::meta::access_context::unchecked())) {
        // offset_of(m).bytes is std::ptrdiff_t (signed); a field offset is
        // non-negative, so cast to size_t (avoids -Werror=narrowing/sign-conversion).
        const std::size_t off{
            static_cast<std::size_t>(std::meta::offset_of(m).bytes)};
        if (off > running)
            out[i++] = {"", std::define_static_string("|V" +
                                                      ai_uint_string(off - running))};
        out[i++] = {std::define_static_string(std::meta::identifier_of(m)),
                    std::define_static_string(numpy_typestr(std::meta::type_of(m)))};
        running = off + std::meta::size_of(std::meta::type_of(m));
    }
    if (running < std::meta::size_of(E))
        out[i++] = {"", std::define_static_string(
                            "|V" + ai_uint_string(std::meta::size_of(E) - running))};
    return out;
}

} // namespace welder::inline v0::rods::python
