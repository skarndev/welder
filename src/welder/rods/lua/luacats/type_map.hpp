#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <welder/bind_traits.hpp> // is_unary_operator
#include <welder/bindable.hpp>    // the STL-wrapper template names + reflect layer

/** @file
    The low-level rendering primitives of the LuaCATS stub backend: the C++→LuaCATS
    **type map**, the C++-operator → `---@operator` name map, the "native" caster
    trait, and the small text helpers the document assembler
    (`<welder/rods/lua/luacats/document.hpp>`) and the backend share.

    The runtime sol2 backend only needed a yes/no caster oracle; a stub needs the
    actual LuaCATS **type name** for every C++ type, which is the one thing living
    here that sol2 did not require. Kept separate from the backend so the type map
    and the document assembler read as distinct layers rather than one soup.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`), like
    the rest of the reflection layer.
*/

namespace welder::rods::luacats {

// --- small text helpers -----------------------------------------------------

/** Append @a text as `--- ` comment lines (each source line prefixed), so a
    multiline summary lands as a LuaCATS description block. A null/empty @a text
    emits nothing.
    @param out  the document buffer to append to.
    @param text the summary text (may be null/empty). */
inline void emit_doc_comment(std::string& out, const char* text) {
    if (!text || !*text)
        return;
    out += "--- ";
    for (const char* c{text}; *c; ++c) {
        out += *c;
        if (*c == '\n' && c[1] != '\0')
            out += "--- ";
    }
    out += '\n';
}

/** A one-line description for a `@field`/`@param`/`@return` tail: newlines in @a
    text collapse to spaces (LuaCATS tags are single-line).
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

// --- the C++ -> LuaCATS type map --------------------------------------------

/** The dotted LuaCATS name of a namespace-or-type reflection: its own identifier
    prefixed by each enclosing named namespace, joined with `.` (so
    `geometry::Point` → `"geometry.Point"`). Class scopes and the global namespace
    contribute nothing.
    @param ent a reflection of the namespace or type to name.
    @return the dotted LuaCATS name. */
consteval std::string qualified_name(std::meta::info ent) {
    std::vector<std::string> parts{};
    if (std::meta::has_identifier(ent))
        parts.emplace_back(std::meta::identifier_of(ent));
    std::meta::info p{std::meta::parent_of(ent)};
    while (p != ^^:: && std::meta::is_namespace(p)) {
        if (std::meta::has_identifier(p))
            parts.emplace_back(std::meta::identifier_of(p));
        p = std::meta::parent_of(p);
    }
    std::string out{};
    for (auto it{parts.rbegin()}; it != parts.rend(); ++it) {
        if (!out.empty())
            out += '.';
        out += *it;
    }
    return out;
}

/** Evaluate a standard unary type-trait *variable template* (e.g.
    `^^std::is_integral_v`) on the type @a t.
    @param trait_var a reflection of the trait variable template.
    @param t a reflection of the type to test.
    @return the trait's `bool` value for @a t. */
consteval bool type_trait(std::meta::info trait_var, std::meta::info t) {
    return std::meta::extract<bool>(std::meta::substitute(trait_var, {t}));
}

/** Whether @a type (already a bare type) is a listed element-wise wrapper whose
    LuaCATS spelling this map renders.
    @param type a reflection of the (cv/ref-stripped) type to classify.
    @param[out] tmpl_out receives @a type's class template when it is a wrapper.
    @return true iff @a type is a class template specialization. */
consteval bool is_wrapper(std::meta::info type, std::meta::info& tmpl_out) {
    if (!std::meta::has_template_arguments(type))
        return false;
    tmpl_out = std::meta::template_of(type);
    return true;
}

/** The LuaCATS type name for a C++ type reflection.

    Strips cv/ref/pointer, then: known STL wrappers render structurally (recursing
    on their value arguments); `std::string`/`std::string_view`/`char*` → `string`;
    `bool` → `boolean`; integral → `integer`; floating → `number`; a class/enum →
    its @ref qualified_name; anything else → `any`. The recursion mirrors welder's
    bindability wrapper table so a `std::vector<Point>` renders as `Point[]`.
    @param type a reflection of the type to render.
    @return the LuaCATS type string. */
consteval std::string lua_type_string(std::meta::info type) {
    namespace m = std::meta;
    const m::info w{m::dealias(m::substitute(^^std::remove_cvref_t, {type}))};

    // String-like (handled before the arithmetic/class buckets).
    if (w == m::dealias(^^std::string) || w == m::dealias(^^std::string_view))
        return "string";
    if (type_trait(^^std::is_pointer_v, w)) {
        const m::info pointee{m::dealias(m::substitute(
            ^^std::remove_cv_t, {m::substitute(^^std::remove_pointer_t, {w})}))};
        if (pointee == ^^char)
            return "string";
        return lua_type_string(pointee); // a pointer to a value type = that type
    }

    // Element-wise STL wrappers.
    m::info tmpl{};
    if (is_wrapper(w, tmpl)) {
        const auto args{m::template_arguments_of(w)};
        auto elem = [&](std::size_t i) { return lua_type_string(args[i]); };
        if (tmpl == ^^std::vector || tmpl == ^^std::list || tmpl == ^^std::deque ||
            tmpl == ^^std::set || tmpl == ^^std::multiset ||
            tmpl == ^^std::unordered_set || tmpl == ^^std::unordered_multiset ||
            tmpl == ^^std::array)
            return elem(0) + "[]";
        if (tmpl == ^^std::map || tmpl == ^^std::multimap ||
            tmpl == ^^std::unordered_map || tmpl == ^^std::unordered_multimap)
            return "table<" + elem(0) + ", " + elem(1) + ">";
        if (tmpl == ^^std::optional)
            return elem(0) + "?";
        if (tmpl == ^^std::shared_ptr || tmpl == ^^std::unique_ptr)
            return elem(0);
        if (tmpl == ^^std::pair)
            return "{ [1]: " + elem(0) + ", [2]: " + elem(1) + " }";
        if (tmpl == ^^std::variant) {
            std::string out{};
            for (std::size_t i{0}; i < args.size(); ++i)
                out += (i ? "|" : "") + lua_type_string(args[i]);
            return out;
        }
        // std::tuple and any other specialization fall through to the class map.
    }

    if (w == ^^bool)
        return "boolean";
    if (type_trait(^^std::is_integral_v, w))
        return "integer";
    if (type_trait(^^std::is_floating_point_v, w))
        return "number";
    if (m::is_enum_type(w) || m::is_class_type(w))
        return qualified_name(w);
    return "any";
}

/** @ref lua_type_string as a static-storage C string, callable on a constant
    type reflection.
    @param type a reflection of the type to render.
    @return the LuaCATS type string, in static storage. */
consteval const char* lua_type(std::meta::info type) {
    return std::define_static_string(lua_type_string(type));
}

/** Whether the backend converts @a U without welder registering a type: scalars,
    strings and `char*`. Classes and enums are program-defined, so they must be
    welded — mirrors the sol2 oracle without depending on sol2.
    @tparam U the type to classify. */
template <class U>
inline constexpr bool is_native_lua =
    std::is_arithmetic_v<U> ||
    std::is_same_v<std::remove_cv_t<U>, std::string> ||
    std::is_same_v<std::remove_cv_t<U>, std::string_view> ||
    std::is_same_v<std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<U>>>,
                   char>;

// --- the C++ -> LuaCATS operator map ----------------------------------------

/** A member operator's LuaCATS `---@operator` name, or `nullptr` if not rendered.

    LuaCATS names only the arithmetic / bitwise / call metamethods; unary vs binary
    is by arity (a member operator takes 0 parameters when unary). The set is exactly
    lua-language-server's `vm.OP_*_MAP` — `add sub mul div mod pow idiv band bor bxor
    shl shr concat` (binary), `unm bnot len` (unary), `call` — so anything we return
    here must be one of those or the language server rejects the stub with
    `unknown-operator`. This mirrors the sol2 runtime metamethod map (`operator_mm`),
    minus one distinction: sol2 `#if`-gates the bitwise metamethods to Lua ≥ 5.3,
    whereas the stub carries no Lua headers (pure reflection + text), so it documents
    the 5.3+ surface unconditionally — the stub's target version is the reader's
    `.luarc.json runtime.version`, not a compile-time `LUA_VERSION_NUM`.

    Deliberately absent: **comparison** (`==`/`<`/`<=`) and **subscript** (`[]`).
    They have no `---@operator` spelling — lua-language-server always permits `==`
    (yielding boolean) and models indexing through `---@field [key] value`, so
    there is nothing to annotate. They still bind at *runtime* (sol2 `__eq`/`__lt`/
    `__le`/`__index`); the stub simply can't type them, so we drop them here rather
    than emit an annotation the language server can't consume.
    @param f a reflection of the operator function.
    @return the `---@operator` name (static storage), or `nullptr`. */
consteval const char* operator_luacats(std::meta::info f) {
    using std::meta::operators;
    const bool unary{::welder::detail::is_unary_operator(f)};
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:    return unary ? nullptr : "add";
        case operators::op_minus:   return unary ? "unm" : "sub";
        case operators::op_star:    return unary ? nullptr : "mul";
        case operators::op_slash:   return "div";
        case operators::op_percent: return "mod";
        case operators::op_parentheses: return "call";
        // Bitwise (Lua 5.3+). C++ operator^ is XOR (→ bxor), NOT power (Lua ^).
        case operators::op_caret:   return "bxor";
        case operators::op_tilde:   return unary ? "bnot" : nullptr;
        case operators::op_ampersand: return unary ? nullptr : "band"; // unary & = address-of
        case operators::op_pipe:            return "bor";
        case operators::op_less_less:       return "shl";
        case operators::op_greater_greater: return "shr";
        default: return nullptr; // ==/</<=/[], !=/>/>=: not a LuaCATS @operator
    }
}

} // namespace welder::rods::luacats
