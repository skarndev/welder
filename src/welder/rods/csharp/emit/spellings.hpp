#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <utility>

#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **One C++ type, three spellings.** Every participating type is written out
    three times, once per audience, and each has its own rules:

    - @ref welder::rods::csharp::shim_wire_spelling — the C-ABI type the native
      thunk's signature declares (`std::int32_t`, `welder_seq_wire`, `void*`);
    - @ref welder::rods::csharp::pinvoke_type — the managed type the
      `[LibraryImport]` declaration uses, which differs from the public one
      (a string is `string` in but `IntPtr` out; a class parameter is its
      `SafeHandle` subclass, for premature-collection safety on the call);
    - @ref welder::rods::csharp::public_type / @ref welder::rods::csharp::public_return_type
      — the idiomatic type the wrapper's user sees (`T?`, `T[]`, a ValueTuple,
      a generated container wrapper).

    The two variable templates @ref welder::rods::csharp::wire_param_v and
    @ref welder::rods::csharp::wire_return_v exist for the gcc-16 reason
    documented in `<welder/rods/csharp/emit/refs.hpp>`.
*/

namespace welder::inline v0::rods::csharp {

/** The fixed-width C ABI wire spelling the shim signature uses for @a type.
    Marshallability was already enforced (@ref require_marshallable) by the
    caller, so `unsupported` cannot reach this.
    @param type      the param/return type reflection.
    @param is_return true when spelling a return position (tuple and
                     shared_ptr wires differ by direction).
    @return the C-ABI type spelling. */
consteval const char* shim_wire_spelling(std::meta::info type, bool is_return) {
    switch (classify(type)) {
        case marshal_kind::void_:       return "void";
        case marshal_kind::scalar:      return scalar_spell(type).c_abi;
        case marshal_kind::boolean:     return "bool";
        case marshal_kind::utf8_string: return is_return ? "const char*"
                                                         : "const char*";
        case marshal_kind::enum_:       return enum_wire_spell(type).c_abi;
        case marshal_kind::optional_:   return "welder_opt_wire";
        case marshal_kind::seq_value:   return "welder_seq_wire";
        case marshal_kind::seq_string:  return "welder_seq_wire";
        case marshal_kind::tuple_value:
            return is_return ? "welder_seq_wire" : "const welder_opt_wire*";
        case marshal_kind::shared_ptr_:
            return is_return ? "welder_sp_wire" : "void*";
        case marshal_kind::unique_ptr_: return "void*";
        default:                        return "void*"; // handle
    }
}

/** @ref shim_wire_spelling in PARAMETER position, as a constant-initialized
    variable template (the gcc-16 consteval-in-runtime-expression workaround —
    see `<welder/rods/csharp/emit/refs.hpp>`).
    @tparam T the parameter type reflection. */
template <std::meta::info T>
inline constexpr const char* wire_param_v = shim_wire_spelling(T, false);
/** @ref shim_wire_spelling in RETURN position (same workaround).
    @tparam T the return type reflection. */
template <std::meta::info T>
inline constexpr const char* wire_return_v = shim_wire_spelling(T, true);
/** The managed type the P/Invoke declaration uses for @a Type. @a is_return
    switches a string between its `in` (`string`) and `out` (`IntPtr`,
    caller-freed) forms; a welded-class parameter is typed as its `SafeHandle`
    subclass (premature-collection safety on the call), a return as `IntPtr`.
    @tparam Type  the param/return type reflection.
    @tparam Style the name style (unused by the wire spellings themselves,
                  threaded for the type references).
    @param is_return true when spelling a return position.
    @return the P/Invoke-declaration type (may carry reference placeholders). */
template <std::meta::info Type, class Style>
std::string pinvoke_type(bool is_return) {
    constexpr marshal_kind k{classify(Type)};
    if constexpr (k == marshal_kind::void_) return "void";
    else if constexpr (k == marshal_kind::scalar) {
        constexpr const char* s{scalar_spell(Type).cs};
        return s;
    } else if constexpr (k == marshal_kind::boolean) return "bool";
    else if constexpr (k == marshal_kind::utf8_string)
        return is_return ? "IntPtr" : "string";
    else if constexpr (k == marshal_kind::enum_)
        return type_ref<bare(Type)>();
    else if constexpr (k == marshal_kind::optional_)
        return "WelderOptWire";
    else if constexpr (k == marshal_kind::seq_value ||
                       k == marshal_kind::seq_string)
        return "WelderSeqWire";
    else if constexpr (k == marshal_kind::tuple_value)
        return is_return ? "WelderSeqWire" : "IntPtr";
    else if constexpr (k == marshal_kind::shared_ptr_)
        return is_return ? "WelderSpWire" : "IntPtr";
    else if constexpr (k == marshal_kind::unique_ptr_)
        return "IntPtr";
    else if constexpr (k == marshal_kind::map_ref)
        return is_return ? std::string{"IntPtr"}
                         : container_ref<bare(Type)>() + "Handle";
    else if constexpr (k == marshal_kind::seq_ref)
        return is_return ? std::string{"IntPtr"}
                         : container_ref<bare(Type)>() + "Handle";
    else // handle
        return is_return ? std::string{"IntPtr"}
                         : type_ref<bare(Type)>() + "Handle";
}
template <std::meta::info Type, std::size_t... J>
std::string tuple_public_type(std::index_sequence<J...>);

/** The public C# type the wrapper API exposes for @a Type (`T?`, `T[]`, a
    ValueTuple, a generated container wrapper's name, …).
    @tparam Type  the param/return type reflection.
    @tparam Style the name style.
    @return the idiomatic C# spelling (may carry reference placeholders). */
template <std::meta::info Type, class Style>
std::string public_type() {
    constexpr marshal_kind k{classify(Type)};
    if constexpr (k == marshal_kind::void_) return "void";
    else if constexpr (k == marshal_kind::scalar) {
        constexpr const char* s{scalar_spell(Type).cs};
        return s;
    } else if constexpr (k == marshal_kind::boolean) return "bool";
    else if constexpr (k == marshal_kind::utf8_string) return "string";
    else if constexpr (k == marshal_kind::optional_) {
        constexpr marshal_kind pk{classify(optional_payload(bare(Type)))};
        if constexpr (pk == marshal_kind::scalar) {
            constexpr const char* c{
                scalar_spell(optional_payload(bare(Type))).cs};
            return std::string{c} + "?";
        } else if constexpr (pk == marshal_kind::boolean)
            return "bool?";
        else if constexpr (pk == marshal_kind::utf8_string)
            return "string?";
        else // enum_ / handle: nullable of the referenced type
            return type_ref<bare(optional_payload(bare(Type)))>() + "?";
    } else if constexpr (k == marshal_kind::seq_value) {
        if constexpr (classify(sequence_element(bare(Type))) ==
                      marshal_kind::enum_)
            return type_ref<bare(sequence_element(bare(Type)))>() + "[]";
        else {
            constexpr const char* c{
                scalar_spell(sequence_element(bare(Type))).cs};
            return std::string{c} + "[]";
        }
    } else if constexpr (k == marshal_kind::seq_string) {
        return "string[]";
    } else if constexpr (k == marshal_kind::tuple_value)
        return tuple_public_type<Type>(
            std::make_index_sequence<tuple_arity(bare(Type))>{});
    else if constexpr (k == marshal_kind::shared_ptr_ ||
                       k == marshal_kind::unique_ptr_)
        return type_ref<bare(std::meta::template_arguments_of(bare(Type))[0])>() +
               "?";
    else if constexpr (k == marshal_kind::seq_ref ||
                       k == marshal_kind::map_ref)
        return container_ref<bare(Type)>();
    else
        return type_ref<bare(Type)>(); // enum_ / handle
}
/** The C# ValueTuple spelling of pair/tuple @a Type — `(int, string)` — one
    @ref public_type per element.
    @tparam Type the pair/tuple type reflection.
    @tparam J    the element indices (`std::make_index_sequence`).
    @return the parenthesized ValueTuple spelling. */
template <std::meta::info Type, std::size_t... J>
std::string tuple_public_type(std::index_sequence<J...>) {
    std::string out{"("};
    std::size_t i{0};
    ((out += (i++ ? std::string{", "} : std::string{}) +
             public_type<tuple_element_type(bare(Type), J),
                         ::welder::naming::none>()),
     ...);
    return out + ")";
}
/** The public C# RETURN type: like @ref public_type, plus the `?` nullable
    marker for a pointer-flavor welded-class return (a C++ `nullptr` maps to
    C# `null`).
    @tparam R     the return type reflection.
    @tparam Style the name style.
    @return the idiomatic C# return spelling. */
template <std::meta::info R, class Style>
std::string public_return_type() {
    if constexpr (classify(R) == marshal_kind::handle ||
                  classify(R) == marshal_kind::seq_ref ||
                  classify(R) == marshal_kind::map_ref) {
        if constexpr (handle_return_nullable(R))
            return public_type<R, Style>() + "?";
        else
            return public_type<R, Style>();
    } else {
        return public_type<R, Style>();
    }
}
} // namespace welder::inline v0::rods::csharp
