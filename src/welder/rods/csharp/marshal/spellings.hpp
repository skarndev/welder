#pragma once
#include <cstddef>
#include <filesystem>
#include <meta>
#include <string>
#include <type_traits>

#include <welder/rods/csharp/marshal/classify.hpp>
#include <welder/rods/csharp/reflect/symbols.hpp>

/** @file
    The **two spellings** every type needs, and the caster oracle that decides
    which types need none.

    Unlike a runtime rod, the C# backend emits *two* coordinated artifacts — an
    `extern "C"` shim (compiled into a shared library **with reflection enabled**,
    against the same welded header) and a `[LibraryImport]` C# wrapper — so every
    scalar type needs two spellings: the fixed-width C ABI type the shim signature
    uses (`std::int32_t`, …) and the managed type the P/Invoke declaration uses
    (`int`, …). @ref welder::rods::csharp::scalar_spelling carries the pair.

    Welded class types do not cross by value; they cross as an opaque handle
    (`void*` ⇄ `System.IntPtr`), so the class *name* is supplied by the emission
    layer (which has the name style), not here. A welded enum crosses as its
    underlying type, mirrored managed-side as `enum : <underlying>`.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

/** The two spellings of a scalar/bool type: the fixed-width C ABI type the shim
    uses and the managed type the P/Invoke declaration uses. */
struct scalar_spelling {
    const char* c_abi{}; /**< e.g. `"std::int32_t"`. */
    const char* cs{};    /**< e.g. `"int"`. */
};

/** The C-ABI + C# spellings of an arithmetic (non-bool) scalar @a type, chosen by
    width and signedness so the two sides agree byte-for-byte.
    @param type a reflection of the scalar type (cv/ref stripped internally).
    @return the paired spelling. */
consteval scalar_spelling scalar_spell(std::meta::info type) {
    namespace m = std::meta;
    const m::info w{bare(type)};
    if (type_trait(^^std::is_floating_point_v, w))
        return m::size_of(w) == 4 ? scalar_spelling{"float", "float"}
                                  : scalar_spelling{"double", "double"};
    const bool sgn{type_trait(^^std::is_signed_v, w)};
    switch (m::size_of(w)) {
        case 1: return sgn ? scalar_spelling{"std::int8_t", "sbyte"}
                           : scalar_spelling{"std::uint8_t", "byte"};
        case 2: return sgn ? scalar_spelling{"std::int16_t", "short"}
                           : scalar_spelling{"std::uint16_t", "ushort"};
        case 4: return sgn ? scalar_spelling{"std::int32_t", "int"}
                           : scalar_spelling{"std::uint32_t", "uint"};
        default: return sgn ? scalar_spelling{"std::int64_t", "long"}
                            : scalar_spelling{"std::uint64_t", "ulong"};
    }
}

/** The spelling pair of a welded enum's wire form — its underlying type's.
    @param type a reflection of the enum type.
    @return the underlying type's paired spelling. */
consteval scalar_spelling enum_wire_spell(std::meta::info type) {
    return scalar_spell(std::meta::underlying_type(bare(type)));
}

/** The exact C++ spelling of a map key/mapped-value type for the generated
    map thunks' template arguments. Type IDENTITY matters (the shim re-derives
    the member's map through this respelling), so a fundamental type keeps its
    own spelling (`display_string_of` — never a fixed-width alias, which could
    name a different type), `std::string` its portable alias, and a welded
    class/enum its qualified path.
    @param T a reflection of the leaf type.
    @return the C++ spelling. */
consteval std::string leaf_cpp_spelling(std::meta::info T) {
    const marshal_kind k{classify(T)};
    if (k == marshal_kind::utf8_string)
        return "std::string";
    if (k == marshal_kind::handle || k == marshal_kind::enum_)
        return qualified_cpp_name(bare(T));
    return std::string{std::meta::display_string_of(bare(T))};
}

/** The symbol/name token a map key or mapped value contributes (leaf kinds by
    their C# spelling, a welded class or enum by its underscore path).
    @param T a reflection of the leaf type.
    @return the identifier-safe token. */
consteval std::string map_token(std::meta::info T) {
    const marshal_kind k{classify(T)};
    if (k == marshal_kind::utf8_string)
        return "str";
    if (k == marshal_kind::boolean)
        return "bool";
    if (k == marshal_kind::scalar)
        return std::string{scalar_spell(T).cs};
    return underscore_path(bare(T));
}

// --- the caster oracle leaf -------------------------------------------------

/** Whether the backend converts @a U without welder registering a type: scalars and
    strings. Classes and enums are program-defined, so they must be welded (crossing
    as a handle / a mirrored `enum`) — mirrors the other rods' oracles.

    `std::filesystem::path` counts as a string: .NET spells filesystem paths as
    `string` (`System.IO.Path` is a static helper over strings, not a path type),
    so a path crosses as UTF-8 text exactly like `std::string` — the same call the
    Python rods' framework casters make. `std::byte` counts as a scalar: it is the
    standard *raw byte*, and C# spells that `byte` (@ref classify).
    @tparam U the type to classify. */
template <class U>
inline constexpr bool is_native_dotnet =
    (std::is_arithmetic_v<U> && !std::is_enum_v<std::remove_cvref_t<U>>) ||
    std::is_same_v<std::remove_cv_t<U>, std::byte> ||
    std::is_same_v<std::remove_cv_t<U>, std::string> ||
    std::is_same_v<std::remove_cv_t<U>, std::string_view> ||
    std::is_same_v<std::remove_cv_t<U>, std::filesystem::path> ||
    std::is_same_v<std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<U>>>,
                   char>;

} // namespace welder::inline v0::rods::csharp
