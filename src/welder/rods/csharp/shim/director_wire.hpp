#pragma once
#include <cstdlib>
#include <meta>
#include <string>
#include <utility>

#include <welder/rods/csharp/shim/convert.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The **director callback's** half of the wire conversion: arguments going
    *out* to a managed override, and its result coming *back* into C++.

    A director slot runs in the opposite direction from every other thunk — C++
    calls managed code — so it needs the mirror image of
    `<welder/rods/csharp/shim/convert.hpp>`: @ref welder::rods::csharp::shim::to_wire_arg
    turns a C++ argument into its wire form (owning any storage the wire pointer
    needs for the duration of the call), and
    @ref welder::rods::csharp::shim::from_wire_return turns the callback's wire
    result into the override's declared C++ return type.

    The generated director subclass itself is emitted as text — see
    `<welder/rods/csharp/directors.hpp>` for the model and
    `<welder/rods/csharp/emit/directors.hpp>` for the emitter.
*/

namespace welder::inline v0::rods::csharp::shim {

/** Convert a director override's C++ argument (declared type @a P) into its
    wire form, OWNING any storage the wire pointer needs for the duration of
    the callback (the holder lives to the end of the full-expression).
    @tparam P a reflection of the parameter's declared type.
    @param a the C++ argument.
    @return a holder whose `get()` yields the wire value. */
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
    managed thunk cloned it, so no managed lifetime races) and is moved out.
    @tparam R a reflection of the override's declared return type.
    @param w the callback's wire result (deduced).
    @return the C++ return value. */
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
        if constexpr (is_pointer_flavor(R)) {
            // A pointer slot returns a VIEW: the managed override's object
            // (or null) crosses as its raw handle — lifetime is the
            // override's contract, exactly as on the Python rods.
            return static_cast<Bare*>(w);
        } else {
            Bare* p{static_cast<Bare*>(w)};
            Bare v{std::move(*p)};
            delete p;
            return v;
        }
    } else if constexpr (k == marshal_kind::enum_) {
        using E = [:bare(R):];
        return static_cast<E>(w);
    } else {
        using V = [:std::meta::remove_cvref(R):];
        return static_cast<V>(w);
    }
}

} // namespace welder::inline v0::rods::csharp::shim
