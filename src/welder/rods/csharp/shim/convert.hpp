#pragma once
#include <concepts> // expected_error_text's rendering ladder
#include <cstddef>
#include <cstdlib>
#include <format>   // expected_error_text's std::formattable rung
#include <memory>
#include <meta>
#include <sstream>  // expected_error_text's operator<< rung
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/annotations.hpp>              // rv_kind
#include <welder/rods/csharp/shim/wire.hpp>    // the wire structs + caught
#include <welder/rods/csharp/type_map.hpp>     // classify / bare / families

/** @file
    **Wire ⇄ C++ conversion**, in both directions, for the compiled marshalling
    library the generated shim delegates into.

    Two functions carry the traffic:

    - @ref welder::rods::csharp::shim::to_cpp turns one wire argument into the
      C++ argument a parameter of a given declared type receives;
    - @ref welder::rods::csharp::shim::guarded runs a call under the error
      contract and turns its C++ result into the wire form the thunk returns.

    Both are parameterized by the **declared type's reflection**, never by a
    respelled type, and both branch on the same @ref welder::rods::csharp::classify
    the generator used — which is what makes the two artifacts agree by
    construction. @ref welder::rods::csharp::shim::wire_return_type is the
    compiled twin of the generator's spelled wire return type.
*/

namespace welder::inline v0::rods::csharp::shim {

/** Read one pair/tuple element of declared type @a E out of its wire slot
    (the string buffer — dup'd by the sender — is freed here).
    @tparam E a reflection of the element's declared type.
    @param s the wire slot.
    @return the C++ element value. */
template <std::meta::info E>
auto tuple_elem_from_slot(const welder_opt_wire& s) {
    constexpr marshal_kind k{classify(E)};
    using Bare = [:bare(E):];
    if constexpr (k == marshal_kind::utf8_string) {
        Bare v{s.s ? s.s : ""};
        std::free(const_cast<char*>(s.s));
        return v;
    } else if constexpr (k == marshal_kind::handle) {
        return Bare{*static_cast<Bare*>(s.p)}; // borrowed: copy in
    } else if constexpr (type_trait(^^std::is_floating_point_v, ^^Bare)) {
        return static_cast<Bare>(s.f);
    } else { // integral scalar / bool / enum
        return static_cast<Bare>(s.i);
    }
}

/** Write one pair/tuple element into its wire slot (strings dup'd for the
    receiver to free; a welded element heap-copies, owned by the receiver).
    @tparam E a reflection of the element's declared type.
    @param v the C++ element value.
    @return the filled wire slot. */
template <std::meta::info E>
welder_opt_wire tuple_elem_to_slot(const auto& v) {
    constexpr marshal_kind k{classify(E)};
    welder_opt_wire s{};
    s.has = 1;
    if constexpr (k == marshal_kind::utf8_string)
        s.s = dup_utf8(v);
    else if constexpr (k == marshal_kind::handle) {
        using Bare = [:bare(E):];
        s.p = new Bare(v);
    } else if constexpr (type_trait(^^std::is_floating_point_v, bare(E)))
        s.f = static_cast<double>(v);
    else
        s.i = static_cast<std::int64_t>(v);
    return s;
}

/** Convert the wire argument @a w into the C++ argument a parameter of declared
    type @a P receives. Scalars cast; an enum casts up from its underlying wire
    value; a string parameter constructs from the marshalled `const char*` (the
    buffer outlives the call — the P/Invoke layer owns it); a welded class
    dereferences its handle (or passes the pointer through for a `T*` param).
    @tparam P a reflection of the parameter's declared type.
    @param w the wire argument (deduced).
    @return the C++ argument, by value or by reference as @a P requires. */
template <std::meta::info P>
constexpr decltype(auto) to_cpp(auto&& w) {
    constexpr marshal_kind k{classify(P)};
    if constexpr (k == marshal_kind::shared_ptr_) {
        // Borrow: a non-owning aliasing shared_ptr over the wrapper's object.
        using Sp = [:bare(P):];
        using El = typename Sp::element_type;
        return Sp{Sp{}, static_cast<El*>(w)};
    } else if constexpr (is_handle_like(k)) {
        using Bare = [:bare(P):];
        if constexpr (is_pointer_flavor(P))
            return static_cast<Bare*>(w);
        else
            return *static_cast<Bare*>(w);
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
            return Opt{*static_cast<Bare*>(w.p)}; // borrowed: copy in
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
    } else if constexpr (k == marshal_kind::tuple_value) {
        using Tup = [:bare(P):];
        const welder_opt_wire* s{static_cast<const welder_opt_wire*>(w)};
        return [&]<std::size_t... J>(std::index_sequence<J...>) {
            return Tup{tuple_elem_from_slot<tuple_element_type(bare(P), J)>(
                s[J])...};
        }(std::make_index_sequence<tuple_arity(bare(P))>{});
    } else { // scalar / boolean
        using V = [:bare(P):];
        return static_cast<V>(w);
    }
}

/** The C++ type a thunk RETURNS for a callable whose C++ result type is @a R —
    the compiled twin of the generator's spelled wire return type (they agree by
    construction; at worst an implicit same-width conversion bridges them).
    @tparam R a reflection of the declared return type.
    @return a reflection of the wire return type. */
template <std::meta::info R>
consteval std::meta::info wire_return_type() {
    constexpr marshal_kind k{classify(R)};
    if constexpr (k == marshal_kind::void_)
        return ^^void;
    else if constexpr (k == marshal_kind::utf8_string)
        return ^^const char*;
    else if constexpr (k == marshal_kind::enum_)
        return std::meta::underlying_type(bare(R));
    else if constexpr (is_handle_like(k) || k == marshal_kind::unique_ptr_)
        return ^^void*;
    else if constexpr (k == marshal_kind::shared_ptr_)
        return ^^welder_sp_wire;
    else if constexpr (k == marshal_kind::optional_)
        return ^^welder_opt_wire;
    else if constexpr (k == marshal_kind::seq_value ||
                       k == marshal_kind::tuple_value)
        return ^^welder_seq_wire;
    else if constexpr (bare(R) == std::meta::dealias(^^std::byte))
        // A scalar by classification, but a SCOPED ENUM by the language: it
        // will not implicitly convert to the `std::uint8_t` the generator
        // spelled, so name that width here and let the cast be explicit.
        // Its UNDERLYING type, not `^^std::uint8_t` — that name is a
        // using-declaration in libstdc++, which `^^` cannot reflect.
        return std::meta::underlying_type(bare(R));
    else // scalar / boolean
        return bare(R);
}

/** The exception message for the error branch of a `std::expected`.

    `std::expected`'s error type is a user type welder knows nothing about, so
    this picks the first spelling the type actually offers, in decreasing order of
    specificity: an explicit ADL `to_string(e)` (the customization point — define
    one beside your error type and it wins), a `.what()` (an exception-shaped
    error), direct string-ness, a `std::formatter`, or an `operator<<`. A type
    offering none of these is a **designed compile error** naming the fix, rather
    than a silently useless "operation failed".
    @tparam E the error type.
    @param e  the error value.
    @return the rendered message. */
template <class E>
std::string expected_error_text(const E& e) {
    if constexpr (requires { { to_string(e) } -> std::convertible_to<std::string>; })
        return std::string{to_string(e)};
    else if constexpr (requires { { e.what() } -> std::convertible_to<std::string>; })
        return std::string{e.what()};
    else if constexpr (std::convertible_to<E, std::string_view>)
        return std::string{std::string_view{e}};
    else if constexpr (std::formattable<E, char>)
        return std::format("{}", e);
    else if constexpr (requires(std::ostringstream& os) { os << e; }) {
        std::ostringstream os{};
        os << e;
        return os.str();
    } else {
        static_assert(
            false,
            "welder: the error type of this std::expected cannot be rendered as "
            "a message, so the C#/.NET exception it becomes would carry none. "
            "Give it an ADL to_string(e), a .what(), a std::formatter or an "
            "operator<< — or mark::exclude the member for lang::cs.");
    }
}

/** Throw the error branch of a `std::expected` as the exception the thunk's
    catch chain converts into the managed error slot. `std::runtime_error` lands
    on @ref error_code::std_exception → `WelderNativeException` managed-side,
    carrying @ref expected_error_text.
    @tparam E the error type.
    @param e  the error value. */
template <class E>
[[noreturn]] void throw_expected_error(const E& e) {
    throw std::runtime_error{expected_error_text(e)};
}

/** Run @a f under the error contract: convert the C++ result (type @a R,
    under return policy @a Rv for a welded-class result) to its wire form,
    inside the @ref caught boundary.

    A `std::expected` result is **unwrapped** here and only here: the value
    branch continues as a plain @a T return (every downstream spelling already
    saw @a T — `classify`/`bare` peel the wrapper), and the error branch throws,
    reaching the managed side through the `welder_error` slot the thunk already
    carries. That is why C# sees `T Method()` rather than a result object: .NET's
    failure channel *is* the exception.
    @tparam R  a reflection of the declared return type.
    @tparam Rv the resolved return-value policy.
    @tparam F  the callable's type (deduced).
    @param err the error slot to report through.
    @param f   the call to run.
    @return the wire form of @a f's result. */
template <std::meta::info R, ::welder::rv_kind Rv = ::welder::rv_kind::automatic,
          class F>
auto guarded(welder_error* err, F&& f) noexcept -> [:wire_return_type<R>():] {
    using Wire = [:wire_return_type<R>():];
    constexpr marshal_kind k{classify(R)};
    if constexpr (is_expected(R)) {
        // Re-enter with the peeled value type; the inner call's wire type is the
        // same one by construction (wire_return_type classifies, and classify
        // peels), so this returns exactly Wire.
        constexpr std::meta::info V{peel_expected(R)};
        // BY VALUE, never `decltype(auto)`: the payload lives inside the local
        // expected, so deducing a reference to `*r` would hand the outer
        // marshalling a dangling reference the moment this lambda returns.
        using VT = [:std::meta::remove_cvref(V):];
        return guarded<V, Rv>(err, [&f]() -> VT {
            auto r{f()};
            if (!r.has_value())
                throw_expected_error(r.error());
            if constexpr (!std::is_void_v<VT>)
                return std::move(*r);
        });
    } else
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
                return dup_utf8(f());
            }
        } else if constexpr (k == marshal_kind::unique_ptr_) {
            // Ownership transfers: release into an owned handle (may be null).
            auto up{f()};
            return static_cast<void*>(up.release());
        } else if constexpr (k == marshal_kind::shared_ptr_) {
            // Share: box a copy of the shared_ptr; the managed view is pinned
            // by the box (its SafeHandle frees the box, dropping the count).
            using Sp = [:bare(R):];
            Sp sp{f()};
            welder_sp_wire w2{};
            if (sp) {
                w2.obj = sp.get();
                w2.box = new Sp(sp);
            }
            return w2;
        } else if constexpr (is_handle_like(k)) {
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
                    w.s = dup_utf8(*o); // managed side frees
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
        } else if constexpr (k == marshal_kind::tuple_value) {
            const auto t{f()};
            constexpr std::size_t n{tuple_arity(bare(R))};
            auto* buf{static_cast<welder_opt_wire*>(
                std::malloc(sizeof(welder_opt_wire) * n))};
            [&]<std::size_t... J>(std::index_sequence<J...>) {
                ((buf[J] = tuple_elem_to_slot<tuple_element_type(bare(R), J)>(
                      std::get<J>(t))),
                 ...);
            }(std::make_index_sequence<n>{});
            welder_seq_wire tw{};
            tw.data = buf; // managed side reads the slots + welder_free's
            tw.len = static_cast<std::int64_t>(n);
            return tw;
        } else if constexpr (k == marshal_kind::enum_) {
            return static_cast<Wire>(f());
        } else { // scalar / boolean
            return static_cast<Wire>(f());
        }
        if constexpr (!std::is_void_v<Wire>)
            return Wire{};
    });
}

} // namespace welder::inline v0::rods::csharp::shim
