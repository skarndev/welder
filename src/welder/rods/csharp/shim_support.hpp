#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <welder/bind_traits.hpp>          // param_types / aggregate_fields
#include <welder/rods/csharp/type_map.hpp> // classify / bare / lookup layer

/** @file
    The compiled marshalling library the **generated** C# shim delegates into.

    The generated `shim.cpp` is compiled with reflection enabled against the same
    welded header the generator saw, so its thunk bodies need not respell any C++
    type: each is a one-liner delegating to a template here, parameterized by the
    exact member reflection re-derived through the shared lookup layer
    (`named_member` / `ctor_at` / `named_field` in `type_map.hpp`) — the
    splice-don't-respell idiom of the trampolines rod applied to the C ABI. Only
    the `extern "C"` **wire types** (`void*`, `std::int32_t`, `const char*`,
    `welder_error*`, …) and the entity anchor spellings (`^^ns::Type`, `"name"`)
    appear as text in the generated file.

    Every template here also owns the **error contract**: the call is wrapped in
    a catch-all whose result lands in the trailing @ref welder_error out-param
    (code `0` = success), so a C++ exception never unwinds through the C ABI.
    The managed wrapper checks the slot and throws after every P/Invoke.

    Wire conversion rules (Phase 1):
    - scalars/bool cross by value (fixed-width spellings, byte-for-byte);
    - a welded enum crosses as its underlying value;
    - strings cross as UTF-8 (`const char*` in; a malloc'd buffer out, freed by
      the managed side via the emitted `welder_free`);
    - a welded class crosses as an opaque handle; a class-typed **return**
      (value or lvalue reference) is heap-copied into a fresh owned handle —
      exactly pybind11's `automatic` behavior for those categories.
*/

/** The C-ABI error slot every generated thunk takes as its trailing out-param.
    `code` `0` means success; `message` is a malloc'd UTF-8 buffer the managed
    side reads and frees via `welder_free` (null when absent). Defined at global
    scope — it is the wire contract type the `extern "C"` signatures spell. */
struct welder_error {
    std::int32_t code;
    char* message;
};

namespace welder::inline v0::rods::csharp::shim {

/** The error codes the catch chain writes (mirrored by the managed wrapper).
    Phase 1 maps every `std::exception` to @ref std_exception; the finer
    taxonomy (bad_alloc, invalid_argument, …) lands with the exception phase. */
enum class error_code : std::int32_t {
    none = 0,          /**< Success. */
    std_exception = 1, /**< A `std::exception` — `message` carries `what()`. */
    unknown = 6,       /**< A non-`std::exception` C++ exception (`catch (...)`). */
};

/** Duplicate @a s into a malloc'd, NUL-terminated UTF-8 buffer the managed side
    frees via the emitted `welder_free`. */
inline char* dup(std::string_view s) noexcept {
    char* p{static_cast<char*>(std::malloc(s.size() + 1))};
    if (p) {
        std::memcpy(p, s.data(), s.size());
        p[s.size()] = '\0';
    }
    return p;
}

/** Reset @a err to success (every thunk's first act). */
inline void clear(welder_error* err) noexcept {
    if (err) {
        err->code = 0;
        err->message = nullptr;
    }
}

/** Record a failure in @a err. */
inline void set_error(welder_error* err, error_code code,
                      std::string_view msg) noexcept {
    if (err) {
        err->code = static_cast<std::int32_t>(code);
        err->message = dup(msg);
    }
}

/** Convert the wire argument @a w into the C++ argument a parameter of declared
    type @a P receives. Scalars cast; an enum casts up from its underlying wire
    value; a string parameter constructs from the marshalled `const char*` (the
    buffer outlives the call — the P/Invoke layer owns it); a welded class
    dereferences its handle (or passes the pointer through for a `T*` param). */
template <std::meta::info P>
constexpr decltype(auto) to_cpp(auto&& w) {
    constexpr marshal_kind k{classify(P)};
    if constexpr (k == marshal_kind::handle) {
        using Bare = [:bare(P):];
        if constexpr (is_pointer_flavor(P))
            return reinterpret_cast<Bare*>(w);
        else
            return *reinterpret_cast<Bare*>(w);
    } else if constexpr (k == marshal_kind::enum_) {
        using E = [:bare(P):];
        return static_cast<E>(w);
    } else if constexpr (k == marshal_kind::utf8_string) {
        using Bare = [:bare(P):];
        if constexpr (^^Bare == ^^char)
            return static_cast<const char*>(w); // char* param passes through
        else
            return Bare{w ? w : ""}; // std::string / std::string_view
    } else { // scalar / boolean
        using V = [:bare(P):];
        return static_cast<V>(w);
    }
}

/** The C++ type a thunk RETURNS for a callable whose C++ result type is @a R —
    the compiled twin of the generator's spelled wire return type (they agree by
    construction; at worst an implicit same-width conversion bridges them). */
template <std::meta::info R>
consteval std::meta::info wire_return_type() {
    constexpr marshal_kind k{classify(R)};
    if constexpr (k == marshal_kind::void_)
        return ^^void;
    else if constexpr (k == marshal_kind::utf8_string)
        return ^^const char*;
    else if constexpr (k == marshal_kind::enum_)
        return std::meta::underlying_type(bare(R));
    else if constexpr (k == marshal_kind::handle)
        return ^^void*;
    else // scalar / boolean
        return bare(R);
}

/** Run @a f under the error contract: clear @a err, invoke, convert the C++
    result (type @a R) to its wire form, and translate any escaping exception
    into @a err — never letting it unwind through the C ABI. */
template <std::meta::info R, class F>
auto guarded(welder_error* err, F&& f) noexcept -> [:wire_return_type<R>():] {
    using Wire = [:wire_return_type<R>():];
    clear(err);
    constexpr marshal_kind k{classify(R)};
    try {
        if constexpr (k == marshal_kind::void_) {
            f();
        } else if constexpr (k == marshal_kind::utf8_string) {
            using Bare = [:bare(R):];
            if constexpr (^^Bare == ^^char) {
                // A char* return may be null; dup preserves that.
                const char* r{f()};
                return r ? dup(r) : nullptr;
            } else {
                return dup(std::string_view{f()});
            }
        } else if constexpr (k == marshal_kind::handle) {
            using Bare = [:bare(R):];
            // Heap-copy the returned value/reference into an owned handle
            // (pybind11's `automatic` for these categories); pointer returns
            // are rejected at generation until the rv:: mapping lands.
            return static_cast<void*>(new Bare(f()));
        } else if constexpr (k == marshal_kind::enum_) {
            return static_cast<Wire>(f());
        } else { // scalar / boolean
            return static_cast<Wire>(f());
        }
    } catch (const std::exception& e) {
        set_error(err, error_code::std_exception, e.what());
    } catch (...) {
        set_error(err, error_code::unknown, "unknown C++ exception");
    }
    if constexpr (!std::is_void_v<Wire>)
        return Wire{};
}

/** Invoke the exact callable @a Fn (spliced — never overload resolution over
    wire types) with the wire arguments converted per its declared parameter
    types. @a lead is the object reference for a nonstatic member, or nothing. */
template <std::meta::info Fn, class... Lead>
constexpr decltype(auto) invoke_exact(Lead&&... lead) {
    return std::invoke(&[:Fn:], std::forward<Lead>(lead)...);
}

template <std::meta::info Fn, class Tup, std::size_t... J, class... Lead>
constexpr decltype(auto) _invoke_wired(Tup&& wire, std::index_sequence<J...>,
                                       Lead&&... lead) {
    static constexpr auto ps{::welder::detail::param_types<Fn>()};
    return std::invoke(&[:Fn:], std::forward<Lead>(lead)...,
                       to_cpp<ps[J]>(std::get<J>(std::forward<Tup>(wire)))...);
}

/** An instance method thunk body: @a W is the WELDED class the handle points to
    (which may differ from @a Fn's declaring class when a non-welded base's
    member was flattened in — the pointer-to-member invocation converts). */
template <std::meta::info W, std::meta::info Fn, class... Wire>
auto method(void* self, welder_error* err, Wire... w) noexcept {
    using Obj = [:W:];
    auto* obj{reinterpret_cast<Obj*>(self)};
    return guarded<std::meta::return_type_of(Fn)>(err, [&]() -> decltype(auto) {
        if constexpr (sizeof...(Wire) == 0)
            return invoke_exact<Fn>(*obj);
        else
            return _invoke_wired<Fn>(std::forward_as_tuple(w...),
                                     std::index_sequence_for<Wire...>{}, *obj);
    });
}

/** A static-method / free-function thunk body. */
template <std::meta::info Fn, class... Wire>
auto function(welder_error* err, Wire... w) noexcept {
    return guarded<std::meta::return_type_of(Fn)>(err, [&]() -> decltype(auto) {
        if constexpr (sizeof...(Wire) == 0)
            return invoke_exact<Fn>();
        else
            return _invoke_wired<Fn>(std::forward_as_tuple(w...),
                                     std::index_sequence_for<Wire...>{});
    });
}

template <std::meta::info W, std::meta::info Ctor, class Tup, std::size_t... J>
void* _construct_wired(Tup&& wire, std::index_sequence<J...>) {
    using Obj = [:W:];
    static constexpr auto ps{::welder::detail::param_types<Ctor>()};
    return new Obj(to_cpp<ps[J]>(std::get<J>(std::forward<Tup>(wire)))...);
}

/** A declared-constructor thunk body: heap-construct via the exact constructor
    @a Ctor's parameter list (arguments arrive exactly typed, so overload
    resolution selects @a Ctor itself). */
template <std::meta::info W, std::meta::info Ctor, class... Wire>
void* construct(welder_error* err, Wire... w) noexcept {
    clear(err);
    try {
        if constexpr (sizeof...(Wire) == 0)
            return new [:W:]();
        else
            return _construct_wired<W, Ctor>(std::forward_as_tuple(w...),
                                             std::index_sequence_for<Wire...>{});
    } catch (const std::exception& e) {
        set_error(err, error_code::std_exception, e.what());
    } catch (...) {
        set_error(err, error_code::unknown, "unknown C++ exception");
    }
    return nullptr;
}

/** The default-constructor thunk body (the synthesized form has no reflection
    to name). */
template <std::meta::info W>
void* default_construct(welder_error* err) noexcept {
    clear(err);
    try {
        return new [:W:]();
    } catch (const std::exception& e) {
        set_error(err, error_code::std_exception, e.what());
    } catch (...) {
        set_error(err, error_code::unknown, "unknown C++ exception");
    }
    return nullptr;
}

template <std::meta::info W, class Tup, std::size_t... J>
void* _aggregate_wired(Tup&& wire, std::index_sequence<J...>) {
    using Obj = [:W:];
    static constexpr auto fs{::welder::detail::aggregate_fields<Obj>()};
    // C++20 parenthesized aggregate initialization, field-for-field.
    return new Obj(
        to_cpp<std::meta::type_of(fs[J])>(std::get<J>(std::forward<Tup>(wire)))...);
}

/** The synthesized aggregate field-constructor thunk body. */
template <std::meta::info W, class... Wire>
void* aggregate_construct(welder_error* err, Wire... w) noexcept {
    clear(err);
    try {
        if constexpr (sizeof...(Wire) == 0)
            return new [:W:]();
        else
            return _aggregate_wired<W>(std::forward_as_tuple(w...),
                                       std::index_sequence_for<Wire...>{});
    } catch (const std::exception& e) {
        set_error(err, error_code::std_exception, e.what());
    } catch (...) {
        set_error(err, error_code::unknown, "unknown C++ exception");
    }
    return nullptr;
}

/** The copy thunk body backing the managed `Clone()` (the admitted copy
    constructor's spelling — C# has no copy-constructor protocol). */
template <std::meta::info W>
void* clone(void* self, welder_error* err) noexcept {
    using Obj = [:W:];
    clear(err);
    try {
        return new Obj(*reinterpret_cast<const Obj*>(self));
    } catch (const std::exception& e) {
        set_error(err, error_code::std_exception, e.what());
    } catch (...) {
        set_error(err, error_code::unknown, "unknown C++ exception");
    }
    return nullptr;
}

/** The destroy thunk body (the managed `SafeHandle`'s release). Never throws —
    a throwing destructor would terminate, exactly as it should. */
template <std::meta::info W>
void destroy(void* self) noexcept {
    delete reinterpret_cast<[:W:]*>(self);
}

/** A data-member getter thunk body (@a W as in @ref method). A class-typed
    member is heap-copied — the live-reference view lands with the rv:: phase. */
template <std::meta::info W, std::meta::info Mem>
auto field_get(void* self, welder_error* err) noexcept {
    using Obj = [:W:];
    auto* obj{reinterpret_cast<Obj*>(self)};
    return guarded<std::meta::type_of(Mem)>(
        err, [&]() -> decltype(auto) { return std::invoke(&[:Mem:], *obj); });
}

/** A data-member setter thunk body (copy-assigns through the wire value). */
template <std::meta::info W, std::meta::info Mem, class Wire>
void field_set(void* self, welder_error* err, Wire w) noexcept {
    using Obj = [:W:];
    auto* obj{reinterpret_cast<Obj*>(self)};
    guarded<^^void>(err, [&] {
        std::invoke(&[:Mem:], *obj) =
            to_cpp<std::meta::remove_cv(std::meta::type_of(Mem))>(w);
    });
}

/** A namespace-variable getter thunk body. */
template <std::meta::info Var>
auto var_get(welder_error* err) noexcept {
    return guarded<std::meta::type_of(Var)>(
        err, [&]() -> decltype(auto) { return [:Var:]; });
}

/** A namespace-variable setter thunk body. */
template <std::meta::info Var, class Wire>
void var_set(welder_error* err, Wire w) noexcept {
    guarded<^^void>(err, [&] {
        [:Var:] = to_cpp<std::meta::remove_cv(std::meta::type_of(Var))>(w);
    });
}

} // namespace welder::inline v0::rods::csharp::shim
