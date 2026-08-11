#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <meta>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <welder/rods/csharp/shim/convert.hpp> // to_cpp / guarded / caught
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The thunk bodies behind the **generated container wrappers** — welder's
    opaque-container model, carried across the C ABI.

    A `std::vector<Welded>`, `std::array<Welded, N>`, `std::vector<scalar>` field
    or `std::map<K, V>` does not cross by value: the managed side gets a handle
    and a generated wrapper class whose operations call the thunks here. Element
    reads hand back the **address** of the live element, so `v[i].Field = x`
    writes through to the C++ container exactly as `obj.items[i].field = x` does
    in Python under `bind_vector` — that is the whole point of the model.

    The scalar-sequence family additionally exposes `data()` so the managed
    wrapper can build a zero-copy `Span<T>` over the C++ buffer.

    Each family's operations are separate function templates rather than one
    class, because they are P/Invoke targets: a C symbol per operation is the
    unit the generator emits and the managed side imports.
*/

namespace welder::inline v0::rods::csharp::shim {

// --- reference-semantic vector support (the seq_ref family) -----------------

/** `new std::vector<E>` (the C# wrapper's parameterless constructor).
    @tparam E a reflection of the element type.
    @param err the error slot.
    @return the new vector's handle. */
template <std::meta::info E>
void* vec_new(welder_error* err) noexcept {
    using El = [:E:];
    return caught<void*>(
        err, [&]() -> void* { return new std::vector<El>(); });
}

/** Delete a vector the managed wrapper owns.
    @tparam E a reflection of the element type.
    @param self the vector's handle. */
template <std::meta::info E>
void vec_destroy(void* self) noexcept {
    using El = [:E:];
    delete static_cast<std::vector<El>*>(self);
}

/** The vector's element count.
    @tparam E a reflection of the element type.
    @param self the vector's handle.
    @param err  the error slot.
    @return the size. */
template <std::meta::info E>
std::int64_t vec_size(void* self, welder_error* err) noexcept {
    using El = [:E:];
    return caught<std::int64_t>(err, [&]() -> std::int64_t {
        return static_cast<std::int64_t>(
            static_cast<std::vector<El>*>(self)->size());
    });
}

/** A LIVE element view (welder's opaque-container aliasing: the wrapper ties
    it to the vector wrapper managed-side). Bounds-checked (`at`).
    @tparam E a reflection of the element type.
    @param self the vector's handle.
    @param i    the element index.
    @param err  the error slot.
    @return the element's address. */
template <std::meta::info E>
void* vec_get(void* self, std::int64_t i, welder_error* err) noexcept {
    using El = [:E:];
    return caught<void*>(err, [&]() -> void* {
        auto& r{static_cast<std::vector<El>*>(self)->at(
            static_cast<std::size_t>(i))};
        return static_cast<void*>(std::addressof(r));
    });
}

/** Copy-assign an element from a borrowed handle.
    @tparam E a reflection of the element type.
    @param self the vector's handle.
    @param i    the element index.
    @param elem a handle to the source element.
    @param err  the error slot. */
template <std::meta::info E>
void vec_set(void* self, std::int64_t i, void* elem,
             welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        static_cast<std::vector<El>*>(self)->at(
            static_cast<std::size_t>(i)) = *static_cast<El*>(elem);
        return 0;
    });
}

/** Append a copy of a borrowed handle's object.
    @tparam E a reflection of the element type.
    @param self the vector's handle.
    @param elem a handle to the source element.
    @param err  the error slot. */
template <std::meta::info E>
void vec_add(void* self, void* elem, welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        static_cast<std::vector<El>*>(self)->push_back(
            *static_cast<El*>(elem));
        return 0;
    });
}

/** Erase every element.
    @tparam E a reflection of the element type.
    @param self the vector's handle.
    @param err  the error slot. */
template <std::meta::info E>
void vec_clear(void* self, welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        static_cast<std::vector<El>*>(self)->clear();
        return 0;
    });
}

// --- reference-semantic SCALAR-sequence support (live fields + spans) --------

/** The buffer address of a `std::vector<scalar|enum>` — the zero-copy span's
    base pointer.
    @tparam E a reflection of the element type.
    @param self the vector's handle.
    @param err  the error slot.
    @return `data()`. */
template <std::meta::info E>
void* vec_data(void* self, welder_error* err) noexcept {
    using El = [:E:];
    return caught<void*>(err, [&]() -> void* {
        return static_cast<void*>(
            static_cast<std::vector<El>*>(self)->data());
    });
}

/** Append one element crossing BY VALUE on the wire (@a W: the fixed-width
    scalar / the enum's underlying type).
    @tparam E a reflection of the element type.
    @tparam W the wire type carrying the value.
    @param self the vector's handle.
    @param v    the value.
    @param err  the error slot. */
template <std::meta::info E, class W>
void vec_push(void* self, W v, welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        static_cast<std::vector<El>*>(self)->push_back(
            static_cast<El>(v));
        return 0;
    });
}

/** Bulk-assign from a managed buffer (the wrapper's CopyFrom / the implicit
    array conversion).
    @tparam E a reflection of the element type.
    @param self the vector's handle.
    @param data the source buffer.
    @param len  its element count.
    @param err  the error slot. */
template <std::meta::info E>
void vec_fill(void* self, const void* data, std::int64_t len,
              welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        const El* d{static_cast<const El*>(data)};
        static_cast<std::vector<El>*>(self)->assign(
            d, d + static_cast<std::size_t>(len));
        return 0;
    });
}

/** The buffer address of a `std::array<E, N>` — the zero-copy span's base
    pointer.
    @tparam E a reflection of the element type.
    @tparam N the array's extent.
    @param self the array's handle.
    @param err  the error slot.
    @return `data()`. */
template <std::meta::info E, std::size_t N>
void* arr_data(void* self, welder_error* err) noexcept {
    using El = [:E:];
    return caught<void*>(err, [&]() -> void* {
        return static_cast<void*>(
            static_cast<std::array<El, N>*>(self)->data());
    });
}

/** Bulk-assign a fixed array — a wrong-length source is the same designed
    error a length-mismatched managed array raises on the by-value path.
    @tparam E a reflection of the element type.
    @tparam N the array's extent.
    @param self the array's handle.
    @param data the source buffer.
    @param len  its element count (must equal @a N).
    @param err  the error slot. */
template <std::meta::info E, std::size_t N>
void arr_fill(void* self, const void* data, std::int64_t len,
              welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        if (static_cast<std::size_t>(len) != N)
            throw std::invalid_argument{
                "welder: expected a sequence of length " + std::to_string(N)};
        const El* d{static_cast<const El*>(data)};
        std::copy(d, d + N, static_cast<std::array<El, N>*>(self)->begin());
        return 0;
    });
}

// --- reference-semantic fixed-array support (std::array<welded, N>) ----------

/** `new std::array<E, N>` (the C# wrapper's parameterless constructor).
    @tparam E a reflection of the element type.
    @tparam N the array's extent.
    @param err the error slot.
    @return the new array's handle. */
template <std::meta::info E, std::size_t N>
void* arr_new(welder_error* err) noexcept {
    using El = [:E:];
    return caught<void*>(
        err, [&]() -> void* { return new std::array<El, N>{}; });
}

/** Delete an array the managed wrapper owns.
    @tparam E a reflection of the element type.
    @tparam N the array's extent.
    @param self the array's handle. */
template <std::meta::info E, std::size_t N>
void arr_destroy(void* self) noexcept {
    using El = [:E:];
    delete static_cast<std::array<El, N>*>(self);
}

/** A LIVE element view of a fixed array. Bounds-checked (`at`).
    @tparam E a reflection of the element type.
    @tparam N the array's extent.
    @param self the array's handle.
    @param i    the element index.
    @param err  the error slot.
    @return the element's address. */
template <std::meta::info E, std::size_t N>
void* arr_get(void* self, std::int64_t i, welder_error* err) noexcept {
    using El = [:E:];
    return caught<void*>(err, [&]() -> void* {
        auto& r{static_cast<std::array<El, N>*>(self)->at(
            static_cast<std::size_t>(i))};
        return static_cast<void*>(std::addressof(r));
    });
}

/** Copy-assign a fixed array's element from a borrowed handle.
    @tparam E a reflection of the element type.
    @tparam N the array's extent.
    @param self the array's handle.
    @param i    the element index.
    @param elem a handle to the source element.
    @param err  the error slot. */
template <std::meta::info E, std::size_t N>
void arr_set(void* self, std::int64_t i, void* elem,
             welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        static_cast<std::array<El, N>*>(self)->at(
            static_cast<std::size_t>(i)) = *static_cast<El*>(elem);
        return 0;
    });
}

// --- reference-semantic map support (std::map / std::unordered_map) ----------

/** The re-derived map type: ordered or unordered, DEFAULT arguments (classify
    admits only that form).
    @tparam Ordered true for `std::map`, false for `std::unordered_map`.
    @tparam K a reflection of the key type.
    @tparam V a reflection of the mapped type. */
template <bool Ordered, std::meta::info K, std::meta::info V>
using map_t = [:std::meta::substitute(
    Ordered ? ^^std::map : ^^std::unordered_map, {K, V}):];
    // (substitute, not a spelled std::map<[:K:],[:V:]> — gcc-16 mis-substitutes
    // splices nested in an alias template's template-argument list)

/** `new` the map (the C# wrapper's parameterless constructor).
    @tparam O ordered-ness, @tparam K the key type, @tparam V the mapped type.
    @param err the error slot.
    @return the new map's handle. */
template <bool O, std::meta::info K, std::meta::info V>
void* map_new(welder_error* err) noexcept {
    return caught<void*>(err,
                         [&]() -> void* { return new map_t<O, K, V>(); });
}

/** Delete a map the managed wrapper owns.
    @tparam O ordered-ness, @tparam K the key type, @tparam V the mapped type.
    @param self the map's handle. */
template <bool O, std::meta::info K, std::meta::info V>
void map_destroy(void* self) noexcept {
    delete static_cast<map_t<O, K, V>*>(self);
}

/** The map's entry count.
    @tparam O ordered-ness, @tparam K the key type, @tparam V the mapped type.
    @param self the map's handle.
    @param err  the error slot.
    @return the size. */
template <bool O, std::meta::info K, std::meta::info V>
std::int64_t map_size(void* self, welder_error* err) noexcept {
    return caught<std::int64_t>(err, [&]() -> std::int64_t {
        return static_cast<std::int64_t>(
            static_cast<map_t<O, K, V>*>(self)->size());
    });
}

/** Whether the map holds @a k.
    @tparam O ordered-ness, @tparam K the key type, @tparam V the mapped type.
    @tparam WireK the key's wire type (deduced).
    @param self the map's handle.
    @param k    the key, on the wire.
    @param err  the error slot.
    @return true when present. */
template <bool O, std::meta::info K, std::meta::info V, class WireK>
bool map_contains(void* self, WireK k, welder_error* err) noexcept {
    return caught<bool>(err, [&]() -> bool {
        return static_cast<map_t<O, K, V>*>(self)->contains(
            to_cpp<K>(k));
    });
}

/** Mapped-value read: a live view for a welded value (the wrapper pins the
    map), the wire value otherwise. Missing key → out_of_range (`at`).
    @tparam O ordered-ness, @tparam K the key type, @tparam V the mapped type.
    @tparam WireK the key's wire type (deduced).
    @param self the map's handle.
    @param k    the key, on the wire.
    @param err  the error slot.
    @return the mapped value's wire form, or its address for a welded value. */
template <bool O, std::meta::info K, std::meta::info V, class WireK>
auto map_get(void* self, WireK k, welder_error* err) noexcept {
    auto* m{static_cast<map_t<O, K, V>*>(self)};
    if constexpr (classify(V) == marshal_kind::handle) {
        return caught<void*>(err, [&]() -> void* {
            auto& r{m->at(to_cpp<K>(k))};
            return static_cast<void*>(std::addressof(r));
        });
    } else {
        return guarded<V>(err,
                          [&]() -> decltype(auto) { return m->at(to_cpp<K>(k)); });
    }
}

/** Insert-or-assign one entry.
    @tparam O ordered-ness, @tparam K the key type, @tparam V the mapped type.
    @tparam WireK the key's wire type (deduced).
    @tparam WireV the value's wire type (deduced).
    @param self the map's handle.
    @param k    the key, on the wire.
    @param v    the value, on the wire.
    @param err  the error slot. */
template <bool O, std::meta::info K, std::meta::info V, class WireK,
          class WireV>
void map_set(void* self, WireK k, WireV v, welder_error* err) noexcept {
    caught<int>(err, [&]() -> int {
        static_cast<map_t<O, K, V>*>(self)->insert_or_assign(
            to_cpp<K>(k), to_cpp<V>(v));
        return 0;
    });
}

/** Erase one entry.
    @tparam O ordered-ness, @tparam K the key type, @tparam V the mapped type.
    @tparam WireK the key's wire type (deduced).
    @param self the map's handle.
    @param k    the key, on the wire.
    @param err  the error slot.
    @return true when an entry was erased. */
template <bool O, std::meta::info K, std::meta::info V, class WireK>
bool map_remove(void* self, WireK k, welder_error* err) noexcept {
    return caught<bool>(err, [&]() -> bool {
        return static_cast<map_t<O, K, V>*>(self)->erase(to_cpp<K>(k)) >
               0;
    });
}

/** Erase every entry.
    @tparam O ordered-ness, @tparam K the key type, @tparam V the mapped type.
    @param self the map's handle.
    @param err  the error slot. */
template <bool O, std::meta::info K, std::meta::info V>
void map_clear(void* self, welder_error* err) noexcept {
    caught<int>(err, [&]() -> int {
        static_cast<map_t<O, K, V>*>(self)->clear();
        return 0;
    });
}

/** Free a boxed `shared_ptr` copy (the managed SharedBox's release).
    @tparam T a reflection of the pointee type.
    @param box the boxed `std::shared_ptr<T>`. */
template <std::meta::info T>
void sp_free(void* box) noexcept {
    using El = [:T:];
    delete static_cast<std::shared_ptr<El>*>(box);
}

} // namespace welder::inline v0::rods::csharp::shim
