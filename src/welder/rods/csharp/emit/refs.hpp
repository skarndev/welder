#pragma once
#include <meta>
#include <string>

#include <welder/naming.hpp>               // name_of + ent_kind
#include <welder/rods/csharp/type_map.hpp> // classify / spellings / symbols

/** @file
    **References to a reflected entity, as emitted text.**

    Two kinds of thing live here. First, the constant-initialized *variable
    templates* that carry a reflection-derived name into runtime code — the
    workaround gcc-16 forces on this whole emission layer (see the note below).
    Second, the four **placeholder** flavors a type reference is written as:
    a reference is routinely emitted before — or without ever knowing — the
    referent's final name, because the container generators see `std::vector<E>`
    and never `E`'s welding alias, and `add_operator` gets no name style at all.
    The document's render pass resolves them (see
    `<welder/rods/csharp/document/artifacts.hpp>`).

    | Helper | Emits | Resolves to |
    |---|---|---|
    | @ref welder::rods::csharp::type_ref | `\x01raw\x02` | the referent's final C# name |
    | @ref welder::rods::csharp::field_ref | `\x03raw\x04` | the same, identifier-sanitized (handle fields) |
    | @ref welder::rods::csharp::anchor_ref | `\x05raw\x06` | the C++ spelling the shim must splice |
    | @ref welder::rods::csharp::container_ref | `\x01display\x02` | a generated container wrapper's name |

    A gcc-16 note that shapes this whole layer: a `consteval` call whose argument is
    a *local* `constexpr` (not a template parameter) is rejected in a runtime
    expression ("consteval-only expressions are only allowed in a constant-evaluated
    context"), and a `consteval` call sitting in a *not-taken* `switch` branch is still
    evaluated at instantiation (so `qualified_cpp_name(int)` would throw). Both are
    avoided the same way: route every reflection-derived name through a **variable
    template** (forcing constant initialization) and branch with **`if constexpr`** on
    the compile-time `classify(...)` so only the taken branch instantiates.
*/

namespace welder::inline v0::rods::csharp {

/** The `::`-qualified C++ name of class/enum/namespace @a T as a runtime-usable
    static C string. Only instantiated for entities with a spellable path. */
template <std::meta::info T>
inline constexpr const char* cpp_name_v =
    std::define_static_string(qualified_cpp_name(T));

/** The styled C# name of entity @a E (kind @a K) under name style @a Style. */
template <std::meta::info E, class Style, ::welder::ent_kind K>
inline constexpr const char* styled_v = ::welder::name_of<E, lang::cs, Style, K>();

/** The identifier of entity @a E as a runtime-usable static C string. */
template <std::meta::info E>
inline constexpr const char* ident_v =
    std::define_static_string(std::meta::identifier_of(E));

/** The `welder_<underscore-path>` C-symbol prefix of namespace/type @a Ent. */
template <std::meta::info Ent>
inline constexpr const char* upath_v =
    std::define_static_string(underscore_path(Ent));

/** @ref symbol_token as a constant-initialized variable template: @ref upath_v's
    collision-free sibling, for a symbol whose @a Ent may be a class-template
    SPECIALIZATION (a container element, a `shared_ptr` payload). Identical to
    `upath_v` for anything spellable. */
template <std::meta::info Ent>
inline constexpr const char* symtok_v =
    std::define_static_string(symbol_token(Ent));
/** A generated CONTAINER wrapper reference (`std::vector<welded>`): the
    rename key is the specialization's display string — `qualified_cpp_name`
    would collapse every instantiation to `::std::vector`. The final C# name
    ("Vector" + the element's name) is registered at collection
    (@ref welder::rods::csharp::ensure_vector) and itself contains the element placeholder,
    which the render pass's rescan resolves. */
template <std::meta::info C>
std::string container_ref() {
    static constexpr const char* d{
        std::define_static_string(std::meta::display_string_of(C))};
    return std::string{"\x01"} + d + "\x02";
}

/** A welded class/enum reference in a HANDLE-FIELD position: the
    identifier-safe placeholder flavor (a nested type's dotted name sanitizes
    to underscores at render). */
template <std::meta::info Bare>
std::string field_ref() {
    if constexpr (spellable(Bare))
        return std::string{"\x03"} + cpp_name_v<Bare> + "\x04";
    else {
        static constexpr const char* d{
            std::define_static_string(std::meta::display_string_of(Bare))};
        return std::string{"\x03"} + d + "\x04";
    }
}

/** A welded class/enum reference in a SHIM-ANCHOR position: the `\x05…\x06`
    placeholder the shim render resolves to the type's **C++** spelling.

    Deferred rather than spelled on the spot, because the spot may not be able to
    spell it. `qualified_cpp_name` is only correct for a type that HAS a
    qualified name; an alias-welded class-template specialization does not, and
    eagerly spelling one yields its enclosing NAMESPACE — which then reaches the
    shim as `^^ns::detail` and is not a type at all. make_class knows the answer
    (its `Decl` IS the welding alias) and records it; everything needing a C++
    anchor without holding that declaration — the container and smart-pointer
    generators, which see only `std::vector<E>` — emits this instead and lets the
    render pass fill it in. Keyed exactly like @ref type_ref, so the name and
    anchor registries share one key space.
    @see document::apply_type_anchors */
template <std::meta::info Bare>
std::string anchor_ref() {
    if constexpr (spellable(Bare))
        return std::string{"\x05"} + cpp_name_v<Bare> + "\x06";
    else {
        static constexpr const char* d{
            std::define_static_string(std::meta::display_string_of(Bare))};
        return std::string{"\x05"} + d + "\x06";
    }
}

/** The managed type the P/Invoke declaration uses for @a Type. @a is_return
    switches a string between its `in` (`string`) and `out` (`IntPtr`,
    caller-freed) forms; a welded-class parameter is typed as its `SafeHandle`
    subclass (premature-collection safety on the call), a return as `IntPtr`. */
/** A welded class/enum REFERENCE as a render-time placeholder: the final C#
    name is reconciled from the document's rename map at render() (filled by
    make_class/make_enum), so reference spelling never depends on declaration
    order or on a Style reaching the hook (add_operator has none). */
template <std::meta::info Bare>
std::string type_ref() {
    if constexpr (spellable(Bare))
        return std::string{"\x01"} + cpp_name_v<Bare> + "\x02";
    else {
        // An unspellable specialization keys on its display string (its
        // qualified name would collapse); make_class registers that key too.
        static constexpr const char* d{
            std::define_static_string(std::meta::display_string_of(Bare))};
        return std::string{"\x01"} + d + "\x02";
    }
}
/** The lookup OWNER expression for a member declared in the scope spelled
    @a ps: the declaring scope's own `^^` anchor for a flattened base's
    member, else the bound type's @a anchor — which also covers members of
    a PROTECTED nested type, whose spelled name (== @a qual) would be
    inaccessible at the shim's namespace scope. */
inline std::string owner_expr(bool named_parent, const std::string& ps,
                               const std::string& qual,
                               const std::string& anchor) {
    return (named_parent && ps != qual) ? "^^" + ps : anchor;
}

} // namespace welder::inline v0::rods::csharp
