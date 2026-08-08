#pragma once
#include <array>    // ^^std::array (the value-sequence family)
#include <cstddef>
#include <meta>
#include <optional> // ^^std::optional
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <welder/bindable.hpp> // reflect layer + native-caster recursion companions
#include <welder/diag.hpp>     // csharp_unmarshallable

/** @file
    The low-level consteval primitives of the C#/.NET backend: the C++ → **C-ABI**
    and C++ → **C#** type maps, the P/Invoke symbol-**mangling** helper, the
    **member-lookup** functions the generated shim uses to re-derive the exact
    reflection it splices (so the two TUs agree by construction), the "native"
    caster trait, and the small text helpers the document assembler
    (`<welder/rods/csharp/document.hpp>`) and the rod share.

    Unlike a runtime rod, the C# backend emits *two* coordinated artifacts — an
    `extern "C"` shim (compiled into a shared library **with reflection enabled**,
    against the same welded header) and a `[LibraryImport]` C# wrapper — so every
    scalar type needs two spellings: the fixed-width C ABI type the shim signature
    uses (`std::int32_t`, …) and the managed type the P/Invoke declaration uses
    (`int`, …). Welded class types do not cross by value; they cross as an opaque
    handle (`void*` ⇄ `System.IntPtr`), so the class *name* is supplied by the rod
    (which has the name style), not here. A welded enum crosses as its underlying
    type, mirrored managed-side as `enum : <underlying>`.

    Everything here is shared between the **generator** TU (which computes lookup
    indices while emitting) and the **shim** TU (which re-runs the same lookups to
    splice the exact overload) — one definition, two call sites, agreement by
    construction. A lookup that no longer matches (the header changed between
    generation and shim compilation) throws a constexpr exception, failing the
    shim build loudly instead of calling the wrong overload.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

// --- small text helpers -----------------------------------------------------

/** Append @a text as C# `/// ` XML-doc `<summary>` lines, or nothing when empty.
    @param out    the buffer to append to.
    @param indent the leading indentation for each `///` line.
    @param text   the summary text (may be null/empty). */
inline void emit_doc_comment(std::string& out, std::string_view indent,
                             const char* text) {
    if (!text || !*text)
        return;
    out += indent;
    out += "/// <summary>";
    for (const char* c{text}; *c; ++c) {
        if (*c == '\n') {
            out += '\n';
            out += indent;
            out += "/// ";
        } else {
            out += *c;
        }
    }
    out += "</summary>\n";
}

/** Collapse newlines in @a text to spaces (for a single-line `<param>` tail).
    @param text the source text (may be null).
    @return the flattened text, empty if @a text is null. */
inline std::string one_line(const char* text) {
    std::string out{};
    if (!text)
        return out;
    for (const char* c{text}; *c; ++c)
        out += (*c == '\n') ? ' ' : *c;
    return out;
}

// --- name / mangling helpers ------------------------------------------------

/** The `::`-free underscore-joined path of a namespace-or-type reflection, for a C
    symbol prefix (so `geometry::Point` → `"geometry_Point"`). Class scopes and the
    global namespace contribute their identifier / nothing respectively.
    @param ent a reflection of the namespace or type.
    @return the underscore-joined path. */
consteval std::string underscore_path(std::meta::info ent) {
    std::vector<std::string> parts{};
    if (std::meta::has_identifier(ent))
        parts.emplace_back(std::meta::identifier_of(ent));
    std::meta::info p{std::meta::parent_of(ent)};
    while (p != ^^:: &&
           (std::meta::is_namespace(p) || std::meta::is_class_type(p))) {
        if (std::meta::has_identifier(p))
            parts.emplace_back(std::meta::identifier_of(p));
        p = std::meta::parent_of(p);
    }
    std::string out{};
    for (auto it{parts.rbegin()}; it != parts.rend(); ++it) {
        if (!out.empty())
            out += '_';
        out += *it;
    }
    return out;
}

/** The `::`-qualified C++ spelling of a class/enum type, for the anchor
    spellings (`^^ns::Type`) and casts in the emitted shim (so `geometry::Point`
    → `"::geometry::Point"`).
    @param ent a reflection of the class or enum type.
    @return the leading-`::` qualified name. */
consteval std::string qualified_cpp_name(std::meta::info ent) {
    std::vector<std::string> parts{};
    if (std::meta::has_identifier(ent))
        parts.emplace_back(std::meta::identifier_of(ent));
    std::meta::info p{std::meta::parent_of(ent)};
    while (p != ^^:: &&
           (std::meta::is_namespace(p) || std::meta::is_class_type(p))) {
        if (std::meta::has_identifier(p))
            parts.emplace_back(std::meta::identifier_of(p));
        p = std::meta::parent_of(p);
    }
    std::string out{};
    for (auto it{parts.rbegin()}; it != parts.rend(); ++it)
        out += "::" + *it;
    return out;
}

// --- the shared member-lookup layer (generator ⇄ shim agreement) ------------

/** The @a K-th function member of @a owner (class or namespace) named @a name,
    counting ALL same-named function declarations in declaration order —
    participation-independent, so the index is stable however the resolution
    prunes. The generator computes an emitted overload's index with @ref
    index_of_named_member; the generated shim calls this to re-derive the same
    reflection and splice it — the exact overload, never overload resolution
    over wire-typed arguments.
    @param owner a reflection of the declaring class or namespace.
    @param name  the member's identifier.
    @param k     the index among same-named function members.
    @return the member's reflection.
    @throws diag::csharp_member_lookup_mismatch when no such member exists —
    the header drifted since generation, so the shim build fails loudly. */
consteval std::meta::info named_member(std::meta::info owner,
                                       std::string_view name, std::size_t k) {
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_function(m) && std::meta::has_identifier(m) &&
            std::meta::identifier_of(m) == name) {
            if (seen == k)
                return m;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** @ref named_member's inverse: the declaration-order index of function @a fn
    among the same-named function members of its declaring scope. The generator
    emits this index; the shim's @ref named_member re-derives the reflection. */
consteval std::size_t index_of_named_member(std::meta::info fn) {
    const std::meta::info owner{std::meta::parent_of(fn)};
    const std::string_view name{std::meta::identifier_of(fn)};
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_function(m) && std::meta::has_identifier(m) &&
            std::meta::identifier_of(m) == name) {
            if (m == fn)
                return seen;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** The @a K-th constructor of class @a owner in declaration order (copy/move
    included in the count — raw positions, stable on both sides). */
consteval std::meta::info ctor_at(std::meta::info owner, std::size_t k) {
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_constructor(m)) {
            if (seen == k)
                return m;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** @ref ctor_at's inverse: the declaration-order constructor index of @a ctor. */
consteval std::size_t index_of_ctor(std::meta::info ctor) {
    const std::meta::info owner{std::meta::parent_of(ctor)};
    std::size_t seen{0};
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::is_constructor(m)) {
            if (m == ctor)
                return seen;
            ++seen;
        }
    }
    throw diag::csharp_member_lookup_mismatch{};
}

/** The nonstatic data member of @a owner named @a name (unique — fields cannot
    overload), or the namespace-scope variable for a namespace @a owner. */
consteval std::meta::info named_field(std::meta::info owner,
                                      std::string_view name) {
    for (std::meta::info m :
         std::meta::members_of(owner, std::meta::access_context::unchecked())) {
        if ((std::meta::is_nonstatic_data_member(m) ||
             std::meta::is_variable(m)) &&
            std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
            return m;
    }
    throw diag::csharp_member_lookup_mismatch{};
}


/** The declared type of callable @a fn's first parameter — an info-returning
    helper (not a subscript at the use site) because gcc-16 rejects a
    subscripted consteval temporary inside a template argument of a runtime
    expression (the same family of workaround as the variable templates). */
consteval std::meta::info first_param_type(std::meta::info fn) {
    return std::meta::type_of(std::meta::parameters_of(fn)[0]);
}

// --- the C++ -> {C-ABI, C#} type map ----------------------------------------

/** How a value type crosses the C ABI boundary. */
enum class marshal_kind {
    void_,       /**< `void` (return only). */
    scalar,      /**< An arithmetic type passed by value. */
    boolean,     /**< `bool` — one byte, `[MarshalAs(U1)]` on the managed side. */
    utf8_string, /**< `std::string` / `std::string_view` / `char*` — UTF-8. */
    enum_,       /**< A welded enum: crosses as its underlying value (`enum : <u>`). */
    handle,      /**< A welded class: crosses as an opaque `void*`/`IntPtr`. */
    optional_,   /**< `std::optional` of a leaf kind: crosses by value as the
                      fixed `welder_opt_wire` struct ⇄ C# `T?`. */
    seq_value,   /**< `std::vector`/`std::array` of scalar/enum elements:
                      crosses by VALUE (a copy) as `welder_seq_wire` ⇄ `T[]`. */
    seq_ref,     /**< `std::vector` of a WELDED class: crosses as an opaque
                      handle behind a generated reference-semantic C# wrapper
                      (live element views — welder's opaque-container model). */
    unsupported  /**< Not yet representable — see @ref require_marshallable. */
};

/** Is @a type (dealias'd) a specialization of class template @a tmpl? */
consteval bool is_specialization_of(std::meta::info type, std::meta::info tmpl) {
    return std::meta::has_template_arguments(type) &&
           std::meta::template_of(type) == tmpl;
}

/** `std::optional<P>`'s payload type. */
consteval std::meta::info optional_payload(std::meta::info type) {
    return std::meta::template_arguments_of(type)[0];
}

/** A `std::vector`/`std::array` sequence's element type. */
consteval std::meta::info sequence_element(std::meta::info type) {
    return std::meta::template_arguments_of(type)[0];
}

/** Whether @a type is the fixed-size sequence (`std::array`). */
consteval bool is_fixed_sequence(std::meta::info type) {
    return is_specialization_of(type, ^^std::array);
}

/** `std::array<T, N>`'s extent N. */
consteval std::size_t fixed_extent(std::meta::info type) {
    return std::meta::extract<std::size_t>(
        std::meta::template_arguments_of(type)[1]);
}

/** Evaluate a standard unary type-trait variable template on the type @a t. */
consteval bool type_trait(std::meta::info trait_var, std::meta::info t) {
    return std::meta::extract<bool>(std::meta::substitute(trait_var, {t}));
}

/** The cv/ref/pointer-stripped bare type of @a type. */
consteval std::meta::info bare(std::meta::info type) {
    namespace m = std::meta;
    m::info w{m::dealias(m::substitute(^^std::remove_cvref_t, {type}))};
    if (type_trait(^^std::is_pointer_v, w))
        w = m::dealias(m::substitute(
            ^^std::remove_cv_t, {m::substitute(^^std::remove_pointer_t, {w})}));
    return w;
}

/** Classify how @a type crosses the boundary (see @ref marshal_kind). Enums are
    kept apart from scalars — like every other rod they are welded (the C#
    backend emits a real `enum`), so they are *not* a native scalar here.
    @param type a reflection of the (possibly cv/ref/pointer) type.
    @return its marshalling classification. */
consteval marshal_kind classify(std::meta::info type) {
    namespace m = std::meta;
    if (m::dealias(type) == ^^void)
        return marshal_kind::void_;
    const m::info w{bare(type)};
    if (w == m::dealias(^^std::string) || w == m::dealias(^^std::string_view))
        return marshal_kind::utf8_string;
    if (type_trait(^^std::is_pointer_v,
                   m::dealias(m::substitute(^^std::remove_cvref_t, {type})))) {
        // A char* (any cv) is a UTF-8 string; other pointers fall through to
        // their pointee classification below (bare() already peeled one level).
        if (w == ^^char)
            return marshal_kind::utf8_string;
    }
    if (w == ^^bool)
        return marshal_kind::boolean;
    if (m::is_enum_type(w))
        return marshal_kind::enum_;
    if (m::is_arithmetic_type(w))
        return marshal_kind::scalar;
    if (m::is_class_type(w)) {
        // The value-marshalled container family (a LEAF payload only — deeper
        // nesting stays unsupported until it earns a wire representation).
        if (is_specialization_of(w, ^^std::optional)) {
            const marshal_kind pk{classify(optional_payload(w))};
            return (pk == marshal_kind::scalar || pk == marshal_kind::boolean ||
                    pk == marshal_kind::enum_ ||
                    pk == marshal_kind::utf8_string ||
                    pk == marshal_kind::handle)
                       ? marshal_kind::optional_
                       : marshal_kind::unsupported;
        }
        if (is_specialization_of(w, ^^std::vector) ||
            is_specialization_of(w, ^^std::array)) {
            const marshal_kind ek{classify(sequence_element(w))};
            // NOT bool: std::vector<bool> is a bitset, not contiguous bools.
            if (ek == marshal_kind::scalar || ek == marshal_kind::enum_)
                return marshal_kind::seq_value;
            // A welded-class element: reference semantics behind a generated
            // wrapper — vector only (the fixed-size opaque wrapper is pending).
            if (ek == marshal_kind::handle &&
                is_specialization_of(w, ^^std::vector))
                return marshal_kind::seq_ref;
            return marshal_kind::unsupported;
        }
        // Only a class this rod REGISTERS can cross as a handle; any other
        // class (a gate-trusted third-party type, an unlisted container) is a
        // designed diagnostic, never a silently-mistyped void*.
        return ::welder::welded_for(w, lang::cs) ? marshal_kind::handle
                                                 : marshal_kind::unsupported;
    }
    return marshal_kind::unsupported;
}

/** Whether @a type is a pointer (possibly behind cv/ref) — used to split the
    handle kind's pointer flavor from value/reference. */
consteval bool is_pointer_flavor(std::meta::info type) {
    namespace m = std::meta;
    return type_trait(^^std::is_pointer_v,
                      m::dealias(m::substitute(^^std::remove_cvref_t, {type})));
}

/** Enforce the current phase's marshalling coverage on a participating
    param/return type: what the shared bindability gate admits but this backend
    cannot yet carry across the C ABI must fail LOUDLY at generation time, never
    silently emit a corrupting `void*`.

    Currently raised here: the structurally-unsupported kinds (containers
    arrive with the container families). Policy-level rejections (`rv::none`,
    `take_ownership` on a reference) live in @ref handle_return_of.
    @param type      the param/return type reflection.
    @param is_return true when @a type is a return type.
    @throws diag::csharp_unmarshallable when the type cannot cross yet. */
consteval void require_marshallable(std::meta::info type, bool is_return) {
    (void)is_return; // both directions currently share one coverage rule
    if (classify(type) == marshal_kind::unsupported)
        throw diag::csharp_unmarshallable{};
}

/** How a welded-class RETURN crosses, resolved from its category (value /
    lvalue-reference / pointer) and its `[[=welder::return_policy]]` — decided
    here once, consumed by BOTH the generator (the C# side's `owns` flag,
    nullability and owner-reference) and the shim's marshalling layer, so the
    two sides cannot disagree. */
enum class handle_return {
    copy_owned, /**< Heap-copy into a fresh owned handle (pybind11 `automatic`
                     for values and lvalue refs; `rv::copy`). */
    move_owned, /**< Heap-move into a fresh owned handle (`rv::move`). */
    adopt,      /**< Adopt the returned pointer as owned (`automatic` /
                     `take_ownership` on a pointer). */
    view,       /**< A non-owning view (`rv::reference`; `automatic_reference`
                     on a pointer). */
    view_keepalive, /**< A non-owning view that keeps its parent object alive
                     managed-side (`rv::reference_internal`). */
};

/** Resolve @ref handle_return for return type @a R under policy @a rv.
    @throws diag::csharp_unmarshallable for combinations this backend rejects:
    `rv::none` (nanobind-only), and `take_ownership` on an lvalue reference
    (adopting a reference is a double-free trap). */
consteval handle_return handle_return_of(std::meta::info R, rv_kind rv) {
    const bool ptr{is_pointer_flavor(R)};
    switch (rv) {
        case rv_kind::automatic:
            return ptr ? handle_return::adopt : handle_return::copy_owned;
        case rv_kind::automatic_reference:
            return ptr ? handle_return::view : handle_return::copy_owned;
        case rv_kind::take_ownership:
            if (!ptr)
                throw diag::csharp_unmarshallable{};
            return handle_return::adopt;
        case rv_kind::copy:
            return handle_return::copy_owned;
        case rv_kind::move:
            return handle_return::move_owned;
        case rv_kind::reference:
            return handle_return::view;
        case rv_kind::reference_internal:
            return handle_return::view_keepalive;
        default: // rv_kind::none
            throw diag::csharp_unmarshallable{};
    }
}

/** Whether the C# wrapper for this handle return may be `null` (a pointer
    return can carry `nullptr`; values and references cannot). */
consteval bool handle_return_nullable(std::meta::info R) {
    return is_pointer_flavor(R);
}

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

/** The spelling pair of a welded enum's wire form — its underlying type's. */
consteval scalar_spelling enum_wire_spell(std::meta::info type) {
    return scalar_spell(std::meta::underlying_type(bare(type)));
}

// --- the caster oracle leaf -------------------------------------------------

/** Whether the backend converts @a U without welder registering a type: scalars and
    strings. Classes and enums are program-defined, so they must be welded (crossing
    as a handle / a mirrored `enum`) — mirrors the other rods' oracles.
    @tparam U the type to classify. */
template <class U>
inline constexpr bool is_native_dotnet =
    (std::is_arithmetic_v<U> && !std::is_enum_v<std::remove_cvref_t<U>>) ||
    std::is_same_v<std::remove_cv_t<U>, std::string> ||
    std::is_same_v<std::remove_cv_t<U>, std::string_view> ||
    std::is_same_v<std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<U>>>,
                   char>;

} // namespace welder::inline v0::rods::csharp
