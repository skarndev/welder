#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <meta>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <welder/bind_traits.hpp>           // param_types / aggregate_fields
#include <welder/rods/csharp/type_map.hpp>  // classify / bare / lookup layer
#include <welder/rods/csharp/operators.hpp> // named_operator (the generated shim
                                            // splices operator lookups too)
#include <welder/rods/csharp/directors.hpp> // director_slot (the generated shim
                                            // re-derives virtual slots too)

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

/** The by-value wire form of a `std::optional` with a LEAF payload: `has` plus
    the payload in the kind-matching field (ints/bools/enums in `i`, floats in
    `f`, a malloc'd UTF-8 buffer in `s`, an object pointer in `p`). Mirrored
    managed-side as the blittable `WelderOptWire`. */
struct welder_opt_wire {
    std::uint8_t has;
    std::int64_t i;
    double f;
    const char* s;
    void* p;
};

/** The by-value wire form of a scalar/enum sequence: an element-typed buffer +
    length. Returns malloc the buffer (freed managed-side via `welder_free`);
    parameters point at the managed array, pinned for the call. */
struct welder_seq_wire {
    void* data;
    std::int64_t len;
};

namespace welder::inline v0::rods::csharp::shim {

/** The error codes the catch chain writes, mirrored by the managed wrapper's
    `WelderInterop.ThrowIfError` (which maps 2–5 to the matching BCL exception
    types and everything else to `WelderNativeException`). Code 7 is reserved
    for a managed-origin exception round-tripping through a director callback
    (the virtuals phase). */
enum class error_code : std::int32_t {
    none = 0,             /**< Success. */
    std_exception = 1,    /**< A plain `std::exception` — `message` = `what()`. */
    bad_alloc = 2,        /**< `std::bad_alloc` → `OutOfMemoryException`. */
    invalid_argument = 3, /**< `std::invalid_argument` → `ArgumentException`. */
    out_of_range = 4,     /**< `std::out_of_range` → `ArgumentOutOfRangeException`. */
    arithmetic = 5,       /**< overflow/underflow/range → `ArithmeticException`. */
    unknown = 6,          /**< A non-`std::exception` throw (`catch (...)`). */
    managed = 7,          /**< Reserved: a managed exception crossing back. */
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

/** A managed (C#) exception crossing BACK through a director callback: the
    override's error slot re-throws as this type, so when it reaches the next
    thunk boundary the catch chain records it as @ref error_code::managed and
    the managed side rethrows the original message. */
struct managed_exception : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/** Rethrow a director callback's failed error slot as @ref managed_exception
    (frees the slot's message buffer). */
[[noreturn]] inline void rethrow_managed(welder_error* e) {
    std::string msg{e && e->message ? e->message : "managed exception"};
    if (e && e->message)
        std::free(e->message);
    throw managed_exception{msg};
}

/** THE catch chain — every thunk's exception boundary in one place: clear
    @a err, run @a f, translate any escaping C++ exception into the error
    taxonomy (never letting it unwind through the C ABI), and return @a Wire{}
    on failure. The taxonomy catches the std hierarchy most-derived-first. */
template <class Wire, class F>
Wire caught(welder_error* err, F&& f) noexcept {
    clear(err);
    try {
        if constexpr (std::is_void_v<Wire>)
            f();
        else
            return f();
    } catch (const managed_exception& e) {
        set_error(err, error_code::managed, e.what());
    } catch (const std::bad_alloc& e) {
        set_error(err, error_code::bad_alloc, e.what());
    } catch (const std::invalid_argument& e) {
        set_error(err, error_code::invalid_argument, e.what());
    } catch (const std::out_of_range& e) {
        set_error(err, error_code::out_of_range, e.what());
    } catch (const std::overflow_error& e) {
        set_error(err, error_code::arithmetic, e.what());
    } catch (const std::underflow_error& e) {
        set_error(err, error_code::arithmetic, e.what());
    } catch (const std::range_error& e) {
        set_error(err, error_code::arithmetic, e.what());
    } catch (const std::exception& e) {
        set_error(err, error_code::std_exception, e.what());
    } catch (...) {
        set_error(err, error_code::unknown, "unknown C++ exception");
    }
    if constexpr (!std::is_void_v<Wire>)
        return Wire{};
}

/** Convert the wire argument @a w into the C++ argument a parameter of declared
    type @a P receives. Scalars cast; an enum casts up from its underlying wire
    value; a string parameter constructs from the marshalled `const char*` (the
    buffer outlives the call — the P/Invoke layer owns it); a welded class
    dereferences its handle (or passes the pointer through for a `T*` param). */
template <std::meta::info P>
constexpr decltype(auto) to_cpp(auto&& w) {
    constexpr marshal_kind k{classify(P)};
    if constexpr (k == marshal_kind::handle || k == marshal_kind::seq_ref) {
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
    } else if constexpr (k == marshal_kind::optional_) {
        using Opt = [:bare(P):];
        using Pay = [:std::meta::remove_cvref(optional_payload(bare(P))):];
        constexpr marshal_kind pk{classify(optional_payload(bare(P)))};
        if (!w.has)
            return Opt{};
        if constexpr (pk == marshal_kind::utf8_string) {
            Opt o{Pay{w.s ? w.s : ""}};
            std::free(const_cast<char*>(w.s)); // param-direction buffer is ours
            return o;
        } else if constexpr (pk == marshal_kind::handle) {
            using Bare = [:bare(optional_payload(bare(P))):];
            return Opt{*reinterpret_cast<Bare*>(w.p)}; // borrowed: copy in
        } else if constexpr (type_trait(^^std::is_floating_point_v,
                                        ^^Pay)) {
            return Opt{static_cast<Pay>(w.f)};
        } else {
            return Opt{static_cast<Pay>(w.i)};
        }
    } else if constexpr (k == marshal_kind::seq_value) {
        using Seq = [:bare(P):];
        using E = [:std::meta::remove_cvref(sequence_element(bare(P))):];
        const E* d{static_cast<const E*>(w.data)};
        if constexpr (is_fixed_sequence(bare(P))) {
            Seq a{};
            if (static_cast<std::size_t>(w.len) != a.size())
                throw std::invalid_argument{
                    "welder: sequence length does not match std::array extent"};
            for (std::size_t i{0}; i < a.size(); ++i)
                a[i] = d[i];
            return a;
        } else {
            return Seq(d, d + w.len);
        }
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
    else if constexpr (k == marshal_kind::handle ||
                       k == marshal_kind::seq_ref)
        return ^^void*;
    else if constexpr (k == marshal_kind::optional_)
        return ^^welder_opt_wire;
    else if constexpr (k == marshal_kind::seq_value)
        return ^^welder_seq_wire;
    else // scalar / boolean
        return bare(R);
}

/** Run @a f under the error contract: convert the C++ result (type @a R,
    under return policy @a Rv for a welded-class result) to its wire form,
    inside the @ref caught boundary. */
template <std::meta::info R, ::welder::rv_kind Rv = ::welder::rv_kind::automatic,
          class F>
auto guarded(welder_error* err, F&& f) noexcept -> [:wire_return_type<R>():] {
    using Wire = [:wire_return_type<R>():];
    constexpr marshal_kind k{classify(R)};
    return caught<Wire>(err, [&]() -> Wire {
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
        } else if constexpr (k == marshal_kind::handle ||
                             k == marshal_kind::seq_ref) {
            using Bare = [:bare(R):];
            constexpr handle_return hr{handle_return_of(R, Rv)};
            if constexpr (hr == handle_return::view ||
                          hr == handle_return::view_keepalive) {
                if constexpr (is_pointer_flavor(R))
                    return static_cast<void*>(
                        const_cast<Bare*>(static_cast<const Bare*>(f())));
                else
                    return static_cast<void*>(
                        const_cast<Bare*>(std::addressof(f())));
            } else if constexpr (hr == handle_return::adopt) {
                return static_cast<void*>(const_cast<Bare*>(
                    static_cast<const Bare*>(f())));
            } else if constexpr (hr == handle_return::move_owned) {
                if constexpr (is_pointer_flavor(R)) {
                    auto* p{f()};
                    return p ? static_cast<void*>(new Bare(std::move(*p)))
                             : nullptr;
                } else {
                    return static_cast<void*>(new Bare(std::move(f())));
                }
            } else { // copy_owned (a by-value result moves — prvalue)
                if constexpr (is_pointer_flavor(R)) {
                    auto* p{f()};
                    return p ? static_cast<void*>(new Bare(*p)) : nullptr;
                } else {
                    return static_cast<void*>(new Bare(f()));
                }
            }
        } else if constexpr (k == marshal_kind::optional_) {
            using Pay = [:std::meta::remove_cvref(optional_payload(bare(R))):];
            constexpr marshal_kind pk{classify(optional_payload(bare(R)))};
            const auto o{f()}; // materialize (may be a reference return)
            welder_opt_wire w{};
            if (o.has_value()) {
                w.has = 1;
                if constexpr (pk == marshal_kind::utf8_string)
                    w.s = dup(std::string_view{*o}); // managed side frees
                else if constexpr (pk == marshal_kind::handle) {
                    using Bare = [:bare(optional_payload(bare(R))):];
                    w.p = new Bare(*o); // an OWNED copy managed-side
                } else if constexpr (type_trait(^^std::is_floating_point_v,
                                                ^^Pay))
                    w.f = static_cast<double>(*o);
                else
                    w.i = static_cast<std::int64_t>(*o);
            }
            return w;
        } else if constexpr (k == marshal_kind::seq_value) {
            using E = [:std::meta::remove_cvref(sequence_element(bare(R))):];
            const auto seq{f()}; // materialize
            welder_seq_wire w{};
            w.len = static_cast<std::int64_t>(seq.size());
            E* buf{static_cast<E*>(std::malloc(sizeof(E) * seq.size()))};
            for (std::size_t i{0}; i < seq.size(); ++i)
                buf[i] = seq[i];
            w.data = buf; // managed side copies + welder_free's
            return w;
        } else if constexpr (k == marshal_kind::enum_) {
            return static_cast<Wire>(f());
        } else { // scalar / boolean
            return static_cast<Wire>(f());
        }
        if constexpr (!std::is_void_v<Wire>)
            return Wire{};
    });
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
    return guarded<std::meta::return_type_of(Fn),
                   ::welder::return_policy_of(Fn, lang::cs)>(
        err, [&]() -> decltype(auto) {
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
    return guarded<std::meta::return_type_of(Fn),
                   ::welder::return_policy_of(Fn, lang::cs)>(
        err, [&]() -> decltype(auto) {
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
    return caught<void*>(err, [&]() -> void* {
        if constexpr (sizeof...(Wire) == 0)
            return new [:W:]();
        else
            return _construct_wired<W, Ctor>(std::forward_as_tuple(w...),
                                             std::index_sequence_for<Wire...>{});
    });
}

/** @ref construct's director twin: heap-construct the DIRECTOR type @a Dir
    (which inherits @a W's constructors) and hand back the @a W-adjusted
    pointer — the handle convention is "points at the welded type". */
template <std::meta::info Dir, std::meta::info W, std::meta::info Ctor,
          class... Wire>
void* construct_as(welder_error* err, Wire... w) noexcept {
    return caught<void*>(err, [&]() -> void* {
        if constexpr (sizeof...(Wire) == 0)
            return static_cast<[:W:]*>(new [:Dir:]());
        else
            return static_cast<[:W:]*>(
                reinterpret_cast<[:Dir:]*>(_construct_wired<Dir, Ctor>(
                    std::forward_as_tuple(w...),
                    std::index_sequence_for<Wire...>{})));
    });
}

/** @ref default_construct's director twin. */
template <std::meta::info Dir, std::meta::info W>
void* default_construct_as(welder_error* err) noexcept {
    return caught<void*>(
        err, [&]() -> void* { return static_cast<[:W:]*>(new [:Dir:]()); });
}

/** The default-constructor thunk body (the synthesized form has no reflection
    to name). */
template <std::meta::info W>
void* default_construct(welder_error* err) noexcept {
    return caught<void*>(err, [&]() -> void* { return new [:W:](); });
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
    return caught<void*>(err, [&]() -> void* {
        if constexpr (sizeof...(Wire) == 0)
            return new [:W:]();
        else
            return _aggregate_wired<W>(std::forward_as_tuple(w...),
                                       std::index_sequence_for<Wire...>{});
    });
}

/** The copy thunk body backing the managed `Clone()` (the admitted copy
    constructor's spelling — C# has no copy-constructor protocol). */
template <std::meta::info W>
void* clone(void* self, welder_error* err) noexcept {
    using Obj = [:W:];
    return caught<void*>(err, [&]() -> void* {
        return new Obj(*reinterpret_cast<const Obj*>(self));
    });
}

/** The destroy thunk body (the managed `SafeHandle`'s release). Never throws —
    a throwing destructor would terminate, exactly as it should. */
template <std::meta::info W>
void destroy(void* self) noexcept {
    delete reinterpret_cast<[:W:]*>(self);
}

/** A data-member getter thunk body (@a W as in @ref method). A **non-const
    class-typed member** hands out a live, non-owning view aliasing the member
    (the runtime rods' `def_readwrite` reference_internal semantics — the C#
    side ties the view to the parent); a const class member and every other
    kind cross by value as before. */
template <std::meta::info W, std::meta::info Mem>
auto field_get(void* self, welder_error* err) noexcept {
    using Obj = [:W:];
    auto* obj{reinterpret_cast<Obj*>(self)};
    if constexpr ((classify(std::meta::type_of(Mem)) == marshal_kind::handle ||
                   classify(std::meta::type_of(Mem)) ==
                       marshal_kind::seq_ref) &&
                  !std::meta::is_const_type(std::meta::type_of(Mem))) {
        return caught<void*>(err, [&]() -> void* {
            // Two statements on purpose: gcc-16 rejects the spliced member's
            // address when nested inside a larger runtime expression
            // ("consteval-only expressions..."), but binding the reference
            // first is fine.
            auto& r{std::invoke(&[:Mem:], *obj)};
            return static_cast<void*>(&r);
        });
    } else {
        return guarded<std::meta::type_of(Mem)>(
            err, [&]() -> decltype(auto) { return std::invoke(&[:Mem:], *obj); });
    }
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

/** The spaceship-comparison thunk body: evaluate `*obj <=> rhs` through
    C++'s own operator-rewriting rules (so member/free/reversed spaceship
    overloads all resolve exactly as for a C++ caller — the pybind rods'
    synthesized_comparison idiom) and collapse the ordering to a wire int:
    `-1` less, `0` equivalent, `1` greater, `2` unordered (partial_ordering).
    The C# side derives the four relational operators from it. @a P is the
    declared operand type. */
template <std::meta::info W, std::meta::info P, class Wire>
std::int32_t compare(void* self, welder_error* err, Wire w) noexcept {
    using Obj = [:W:];
    auto* obj{reinterpret_cast<Obj*>(self)};
    return caught<std::int32_t>(err, [&]() -> std::int32_t {
        const auto c{*obj <=> to_cpp<P>(w)};
        if (c < 0)
            return -1;
        if (c > 0)
            return 1;
        if (c == 0)
            return 0;
        return 2;
    });
}

/** The stringifier thunk body: run the swept free ostream inserter through
    @ref welder::detail::stringify and dup the text (the managed `ToString()`
    frees it via `welder_free`). */
template <std::meta::info W, std::meta::info Fn>
const char* stringify_text(void* self, welder_error* err) noexcept {
    using Obj = [:W:];
    auto* obj{reinterpret_cast<Obj*>(self)};
    return caught<const char*>(err, [&]() -> const char* {
        return dup(::welder::detail::stringify<Obj, Fn>(*obj));
    });
}

// --- reference-semantic vector support (the seq_ref family) -----------------

/** `new std::vector<E>` (the C# wrapper's parameterless constructor). */
template <std::meta::info E>
void* vec_new(welder_error* err) noexcept {
    using El = [:E:];
    return caught<void*>(
        err, [&]() -> void* { return new std::vector<El>(); });
}

template <std::meta::info E>
void vec_destroy(void* self) noexcept {
    using El = [:E:];
    delete reinterpret_cast<std::vector<El>*>(self);
}

template <std::meta::info E>
std::int64_t vec_size(void* self, welder_error* err) noexcept {
    using El = [:E:];
    return caught<std::int64_t>(err, [&]() -> std::int64_t {
        return static_cast<std::int64_t>(
            reinterpret_cast<std::vector<El>*>(self)->size());
    });
}

/** A LIVE element view (welder's opaque-container aliasing: the wrapper ties
    it to the vector wrapper managed-side). Bounds-checked (`at`). */
template <std::meta::info E>
void* vec_get(void* self, std::int64_t i, welder_error* err) noexcept {
    using El = [:E:];
    return caught<void*>(err, [&]() -> void* {
        auto& r{reinterpret_cast<std::vector<El>*>(self)->at(
            static_cast<std::size_t>(i))};
        return static_cast<void*>(std::addressof(r));
    });
}

/** Copy-assign an element from a borrowed handle. */
template <std::meta::info E>
void vec_set(void* self, std::int64_t i, void* elem,
             welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        reinterpret_cast<std::vector<El>*>(self)->at(
            static_cast<std::size_t>(i)) = *reinterpret_cast<El*>(elem);
        return 0;
    });
}

/** Append a copy of a borrowed handle's object. */
template <std::meta::info E>
void vec_add(void* self, void* elem, welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        reinterpret_cast<std::vector<El>*>(self)->push_back(
            *reinterpret_cast<El*>(elem));
        return 0;
    });
}

template <std::meta::info E>
void vec_clear(void* self, welder_error* err) noexcept {
    using El = [:E:];
    caught<int>(err, [&]() -> int {
        reinterpret_cast<std::vector<El>*>(self)->clear();
        return 0;
    });
}

// --- director (C# virtual-override) support ---------------------------------

/** Convert a director override's C++ argument (declared type @a P) into its
    wire form, OWNING any storage the wire pointer needs for the duration of
    the callback (the holder lives to the end of the full-expression). */
template <std::meta::info P>
auto to_wire_arg(const auto& a) {
    constexpr marshal_kind k{classify(P)};
    if constexpr (k == marshal_kind::handle) {
        using Bare = [:bare(P):];
        if constexpr (is_pointer_flavor(P)) {
            struct hold {
                void* p;
                void* get() const { return p; }
            };
            return hold{const_cast<Bare*>(static_cast<const Bare*>(a))};
        } else {
            struct hold {
                void* p;
                void* get() const { return p; }
            };
            return hold{const_cast<Bare*>(std::addressof(a))};
        }
    } else if constexpr (k == marshal_kind::utf8_string) {
        using Bare = [:bare(P):];
        if constexpr (^^Bare == ^^char) {
            struct hold {
                const char* p;
                const char* get() const { return p; }
            };
            return hold{a};
        } else {
            // std::string passes its own buffer; a string_view materializes a
            // NUL-terminated copy (its data() need not be terminated).
            struct hold {
                std::string s;
                const char* get() const { return s.c_str(); }
            };
            return hold{std::string{a}};
        }
    } else if constexpr (k == marshal_kind::enum_) {
        using U = [:std::meta::underlying_type(bare(P)):];
        struct hold {
            U v;
            U get() const { return v; }
        };
        return hold{static_cast<U>(a)};
    } else { // scalar / boolean
        using V = [:bare(P):];
        struct hold {
            V v;
            V get() const { return v; }
        };
        return hold{static_cast<V>(a)};
    }
}

/** Convert a director callback's wire result back into the override's C++
    return type @a R. A string result arrives as a `welder_dup_utf8` buffer
    (freed here); a class-by-value result arrives as an OWNED heap copy (the
    managed thunk cloned it, so no managed lifetime races) and is moved out. */
template <std::meta::info R>
auto from_wire_return(auto w) -> [:std::meta::remove_cvref(R):] {
    constexpr marshal_kind k{classify(R)};
    if constexpr (k == marshal_kind::utf8_string) {
        std::string s{w ? w : ""};
        if (w)
            std::free(const_cast<char*>(static_cast<const char*>(w)));
        return s;
    } else if constexpr (k == marshal_kind::handle) {
        using Bare = [:bare(R):];
        Bare* p{reinterpret_cast<Bare*>(w)};
        Bare v{std::move(*p)};
        delete p;
        return v;
    } else if constexpr (k == marshal_kind::enum_) {
        using E = [:bare(R):];
        return static_cast<E>(w);
    } else {
        using V = [:std::meta::remove_cvref(R):];
        return static_cast<V>(w);
    }
}

/** The upcast thunk body: adjust a @a From handle to its @a To base subobject
    — a compiled `static_cast`, so multiple/virtual-inheritance offsets are the
    ABI's own. Pure pointer math: no error slot. */
template <std::meta::info From, std::meta::info To>
void* upcast(void* self) noexcept {
    if (!self)
        return nullptr;
    return static_cast<[:To:]*>(reinterpret_cast<[:From:]*>(self));
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
