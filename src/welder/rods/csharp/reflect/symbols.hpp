#pragma once
#include <meta>
#include <string>
#include <vector>

/** @file
    Entity **naming** for the C# backend: the three spellings a reflected type or
    namespace needs on the way out, all derived from its scope chain.

    - @ref welder::rods::csharp::underscore_path — the C-symbol prefix
      (`geometry_Point`);
    - @ref welder::rods::csharp::qualified_cpp_name — the C++ spelling the
      emitted shim anchors on (`::geometry::Point`);
    - @ref welder::rods::csharp::symbol_token — an identifier-safe token that is
      *unique per type*, for symbols built over entities whose scope chain has no
      identifier at all (a class-template specialization).

    @ref welder::rods::csharp::spellable is the predicate that decides between
    the last two: it answers "does this entity have a qualified name a second
    translation unit could write down?".

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

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

/** Whether @a ent's qualified name is SPELLABLE — it and every enclosing
    class scope has an identifier (a class-template specialization segment has
    none, so anything inside one must anchor through its welding alias).
    @param ent a reflection of the entity.
    @return true when @ref qualified_cpp_name yields a name a second TU can write. */
consteval bool spellable(std::meta::info ent) {
    if (!std::meta::has_identifier(ent))
        return false;
    for (std::meta::info p{std::meta::parent_of(ent)};
         std::meta::is_namespace(p) ||
         (std::meta::is_type(p) && std::meta::is_class_type(p));
         p = std::meta::parent_of(p)) {
        if (p == ^^::)
            break;
        if (!std::meta::has_identifier(p))
            return false;
    }
    return true;
}

/** An identifier-safe token that is UNIQUE per type, for a generated C symbol.

    @ref underscore_path alone is not unique: it skips any scope segment without
    an identifier, and a class-template **specialization** has none — so every
    specialization declared in one namespace collapses onto the same token, and
    the container/shared-ptr thunks built from it become duplicate C symbols
    (`std::vector<MapChunk<vanilla>>` and `std::vector<MapChunk<wotlk>>` both
    landed on `welder_vec_..._adt_detail_*`). For an unspellable entity the
    display string is used instead — it spells the template arguments, so it
    distinguishes the specializations — sanitized to identifier characters.
    Spellable entities keep the readable underscore path unchanged.
    @param ent a reflection of the type.
    @return the token: `[A-Za-z0-9_]+`, distinct for distinct types. */
consteval std::string symbol_token(std::meta::info ent) {
    if (spellable(ent))
        return underscore_path(ent);
    std::string out{};
    bool last_us{true}; // suppress a leading underscore run
    for (char c : std::meta::display_string_of(ent)) {
        const bool ok{(c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_'};
        if (ok) {
            out += c;
            last_us = false;
        } else if (!last_us) {
            out += '_';
            last_us = true;
        }
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

} // namespace welder::inline v0::rods::csharp
