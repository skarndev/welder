#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <welder/rods/lua/luacats/type_map.hpp> // lua_type / text helpers
#include <welder/bind_traits.hpp>                    // param_types / aggregate_fields
#include <welder/doc.hpp> // doc_of / param_docs / return_doc_of

/** @file
    The LuaCATS document assembler: how a `---@meta` stub is built up in memory.

    Two layers sit here, both above the type map
    (`<welder/rods/lua/luacats/type_map.hpp>`) and below the backend:
    - **signature rendering** — turning a reflected function/constructor into its
      `---@param`/`---@return` lines and `fun(…)` signature, and grouping overloads
      into the idiomatic one-symbol/`---@overload` shape;
    - the **writer/handle types** (@ref welder::rods::luacats::document and the per-entity `*_writer`s) the
      backend's `module_type`/class-handle/enum-handle deduce to, which accumulate
      text and flush it (RAII, since the driver has no explicit "finish" hook).

    Kept out of `backend.hpp` so those handle types and their shared rendering read
    as their own unit rather than as free functions surrounding the backend struct.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`), like
    the rest of the reflection layer.
*/

namespace welder::inline v0::rods::luacats {

// --- signature rendering ----------------------------------------------------

/** The LuaCATS type name of each parameter of @a Fn, as a splice-ready array
    parallel to `param_docs<Fn>()`.
    @tparam Fn a reflection of the function.
    @return an array of the parameters' LuaCATS type strings (empty if none). */
template <std::meta::info Fn>
consteval auto param_lua_types() {
    // Guard n != 0: param_types<Fn> materializes std::array<info, n> and indexes
    // it, and std::array<T, 0>::operator[] is not usable (as the rest of welder
    // guards too), so it must not be instantiated for a parameterless function.
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    std::array<const char*, n> out{};
    if constexpr (n != 0) {
        constexpr auto types{::welder::detail::param_types<Fn>()};
        for (std::size_t i{0}; i < n; ++i)
            out[i] = std::define_static_string(lua_type_string(types[i]));
    }
    return out;
}

/** The comma-joined argument-name list for @a Fn's declaration line (an unnamed
    parameter becomes `arg<N>`).
    @tparam Fn a reflection of the function.
    @return the argument-name list (e.g. `"x, y"`). */
template <std::meta::info Fn>
std::string arg_list() {
    static constexpr auto pds{::welder::param_docs<Fn>()};
    std::string out{};
    for (std::size_t i{0}; i < pds.size(); ++i) {
        if (i)
            out += ", ";
        out += pds[i].name ? std::string{pds[i].name}
                           : "arg" + std::to_string(i + 1);
    }
    return out;
}

/** Emit the `---@param` lines for @a Fn (name, LuaCATS type, and its `doc`).
    @tparam Fn a reflection of the function.
    @param out the buffer to append the `---@param` lines to. */
template <std::meta::info Fn>
void emit_params(std::string& out) {
    static constexpr auto pds{::welder::param_docs<Fn>()};
    static constexpr auto types{param_lua_types<Fn>()};
    for (std::size_t i{0}; i < pds.size(); ++i) {
        out += "---@param ";
        out += pds[i].name ? std::string{pds[i].name}
                           : "arg" + std::to_string(i + 1);
        out += ' ';
        out += types[i];
        const std::string d{one_line(pds[i].text)};
        if (!d.empty())
            out += ' ' + d;
        out += '\n';
    }
}

/** The `fun(<name>: <type>, …): <ret>` signature of @a Fn, for a `---@overload` line.

    A non-empty @a ret_override forces the return type — used for a constructor,
    whose reflection carries no return type (so `return_type_of` must not be
    instantiated for it: the `is_constructor` guard discards that branch).
    @tparam Fn a reflection of the function or constructor.
    @param ret_override forces the return type when non-empty (for a constructor).
    @return the `fun(…)` signature string. */
template <std::meta::info Fn>
std::string fun_signature(const std::string& ret_override) {
    static constexpr auto pds{::welder::param_docs<Fn>()};
    static constexpr auto types{param_lua_types<Fn>()};
    std::string s{"fun("};
    for (std::size_t i{0}; i < pds.size(); ++i) {
        if (i)
            s += ", ";
        s += pds[i].name ? std::string{pds[i].name} : "arg" + std::to_string(i + 1);
        s += ": ";
        s += types[i];
    }
    s += ")";
    std::string ret{ret_override};
    if (ret.empty()) {
        if constexpr (!std::meta::is_constructor(Fn)) {
            using R = [:std::meta::return_type_of(Fn):];
            if constexpr (!std::is_void_v<R>)
                ret = lua_type(std::meta::return_type_of(Fn));
        }
    }
    if (!ret.empty())
        s += ": " + ret;
    return s;
}

/** One overload of a callee, pre-rendered into the pieces a group needs: the
    summary `doc`, the `---@param` block, the `---@return` line, the `function`
    argument list, and the `fun(…)` signature for a `---@overload` line. */
struct func_overload {
    std::string doc{};      /**< Summary docstring (may be empty). */
    std::string params{};   /**< `---@param …` lines (may be empty). */
    std::string ret_line{}; /**< The `---@return …` line (empty when void). */
    std::string args{};     /**< The `function callee(<args>)` name list. */
    std::string fun_sig{};  /**< The `fun(a: T, …): R` signature. */
};

/** Build the @ref func_overload for function/constructor @a Fn. A non-empty @a
    ret_override supplies the return type for a constructor (which has none).
    @tparam Fn a reflection of the function or constructor.
    @param ret_override the return type to force for a constructor (empty otherwise).
    @return the pre-rendered overload pieces. */
template <std::meta::info Fn>
func_overload build_overload(const std::string& ret_override) {
    func_overload o{};
    if (const char* d{::welder::doc_of<Fn>()})
        o.doc = d;
    emit_params<Fn>(o.params);
    if (!ret_override.empty()) {
        o.ret_line = "---@return " + ret_override + '\n';
    } else if constexpr (!std::meta::is_constructor(Fn)) {
        using R = [:std::meta::return_type_of(Fn):];
        if constexpr (!std::is_void_v<R>) {
            o.ret_line = "---@return ";
            o.ret_line += lua_type(std::meta::return_type_of(Fn));
            const std::string d{one_line(::welder::return_doc_of<Fn>())};
            if (!d.empty())
                o.ret_line += ' ' + d;
            o.ret_line += '\n';
        }
    }
    o.args = arg_list<Fn>();
    o.fun_sig = fun_signature<Fn>(ret_override);
    return o;
}

/** Append `build_overload` for each member of overload group @a Grp to @a out.
    @tparam Grp the overload group (a static array of reflections).
    @tparam I   the group index pack.
    @param out the vector to append the built overloads to. */
template <auto Grp, std::size_t... I>
void collect_overloads(std::vector<func_overload>& out, std::index_sequence<I...>) {
    (out.push_back(build_overload<Grp[I]>({})), ...);
}

/** Emit one documented `function <callee>(…)` for overload group @a sigs, with a
    `---@overload fun(…)` line per *additional* signature — the idiomatic LuaCATS
    shape (one symbol, several signatures), rather than repeated `function` defs.

    @a sigs is non-empty. The "primary" overload — the one whose full docs and
    parameter block are kept — is the first that carries a docstring (else the
    first), so a documented overload's `@param`/summary text survives; the rest, of
    which LuaCATS `---@overload` records only the bare signature, follow it.
    @param out    the document buffer to append to.
    @param callee the fully-qualified LuaCATS name of the function being defined.
    @param sigs   the group's pre-rendered overloads (non-empty). */
inline void render_overload_group(std::string& out, const std::string& callee,
                                  const std::vector<func_overload>& sigs) {
    std::size_t primary{0};
    for (std::size_t i{0}; i < sigs.size(); ++i)
        if (!sigs[i].doc.empty()) {
            primary = i;
            break;
        }
    const func_overload& p{sigs[primary]};
    emit_doc_comment(out, p.doc.empty() ? nullptr : p.doc.c_str());
    out += p.params;
    out += p.ret_line;
    for (std::size_t i{0}; i < sigs.size(); ++i)
        if (i != primary)
            out += "---@overload " + sigs[i].fun_sig + '\n';
    out += "function " + callee + "(" + p.args + ") end\n\n";
}

// --- the document + its writer handles --------------------------------------

/** Rewrite every whole dotted-name token of @a text that is a key in @a renames to
    its mapped value.

    A LuaCATS type *reference* (a `---@field`/`---@param`/`---@return` type, a
    `---@class … : Base` base, a `T[]`/`table<K, V>`/`T?` element) is emitted from
    the type map as the referenced type's *raw* C++ dotted name (@ref
    qualified_name) — the type map sees only a `std::meta::info`, with no access to
    the name style or the referenced type's `weld_as`, both of which need the type as
    a constant. So references are reconciled here instead: each welded class/enum
    registers its raw→styled name as it is declared (where the type *is* a constant),
    and this final pass maps the references to match. Deferring to render() means
    declaration order is irrelevant — every type is registered before any reference
    is rewritten.

    Tokenizing on the identifier+`.` character class makes each qualified name
    atomic, so `geometry.Point` is remapped as a unit: it neither collides with a
    longer name (`geometry.Point3`) nor with a substring (`Point` inside it), and a
    styled declaration name (never a key) is left untouched. Keys are always dotted
    (every welded type sits under the module table), so ordinary prose in a docstring
    is not at risk unless it spells a fully-qualified type path verbatim.
    @param text    the assembled document text.
    @param renames the raw→styled name map (may be empty — then @a text is returned).
    @return the reconciled text. */
inline std::string apply_type_renames(
    std::string_view text,
    const std::vector<std::pair<std::string, std::string>>& renames) {
    if (renames.empty())
        return std::string{text};
    auto is_name_char = [](char c) {
        return c == '.' || c == '_' || (c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    };
    std::string out{};
    out.reserve(text.size());
    for (std::size_t i{0}; i < text.size();) {
        if (!is_name_char(text[i])) {
            out += text[i++];
            continue;
        }
        std::size_t j{i};
        while (j < text.size() && is_name_char(text[j]))
            ++j;
        const std::string_view tok{text.substr(i, j - i)};
        const auto it{std::find_if(renames.begin(), renames.end(),
                                   [&](const auto& r) { return r.first == tok; })};
        out += (it != renames.end()) ? std::string_view{it->second} : tok;
        i = j;
    }
    return out;
}

/** A module/submodule table to declare (`prefix = {}`), with its optional doc. */
struct table_decl {
    std::string prefix{}; /**< The dotted table path, e.g. `"geometry.detail"`. */
    std::string doc{};    /**< Its namespace `doc`, if any. */
};

/** The growing LuaCATS document shared by every writer handle. Class/enum/function
    text lands in @ref body; module tables are collected in @ref tables and
    rendered (shallowest first) ahead of it, since a `geometry.Point` class or a
    `geometry.foo` function needs the `geometry` table to exist. */
struct document {
    std::vector<table_decl> tables{};
    std::string body{};
    /** Raw C++ dotted name → styled/`weld_as` bound name, for reconciling type
        *references* with their declarations at @ref render (see @ref
        apply_type_renames). Populated by @ref record_type_name as each type is made. */
    std::vector<std::pair<std::string, std::string>> type_renames{};

    /** Record (once) a module table to declare, updating its doc if given.
        @param prefix the dotted table path to declare.
        @param doc    the table's namespace `doc`, or `nullptr`. */
    void declare_table(std::string_view prefix, const char* doc = nullptr) {
        for (auto& t : tables)
            if (t.prefix == prefix) {
                if (doc && *doc)
                    t.doc = doc;
                return;
            }
        tables.push_back({std::string{prefix}, doc ? doc : ""});
    }

    /** Register a welded type's declared name so references to it are reconciled at
        @ref render. A no-op when the name is unchanged (the @ref
        welder::naming::none default, or a type the style/`weld_as` leaves alone), so
        the default stub is emitted byte-for-byte as before.
        @param raw    the type's raw C++ dotted name (@ref qualified_name).
        @param styled the type's declared LuaCATS name (its `class_writer::qualified`). */
    void record_type_name(std::string_view raw, std::string_view styled) {
        if (raw == styled)
            return;
        for (const auto& [r, s] : type_renames)
            if (r == raw)
                return;
        type_renames.emplace_back(std::string{raw}, std::string{styled});
    }

    /** The finished stub text: the `---@meta` header, the module tables shallowest
        first, then the accumulated declarations.
        @return the complete LuaCATS document. */
    std::string render() const {
        auto depth = [](const std::string& s) {
            return std::count(s.begin(), s.end(), '.');
        };
        std::vector<table_decl> decls{tables};
        std::stable_sort(decls.begin(), decls.end(),
                         [&](const table_decl& a, const table_decl& b) {
                             return depth(a.prefix) < depth(b.prefix);
                         });
        std::string out{"---@meta\n\n"};
        for (const auto& d : decls) {
            emit_doc_comment(out, d.doc.empty() ? nullptr : d.doc.c_str());
            out += d.prefix + " = {}\n\n";
        }
        out += body;
        // Reconcile type *references* (raw C++ names, as the type map emits them)
        // with their styled/weld_as declarations, now that every type is registered.
        return apply_type_renames(out, type_renames);
    }
};

/** A module handle: the shared document plus this (sub)module's dotted table path.
    Copyable, because the driver's `add_submodule` returns one by value. */
struct module_writer {
    document* doc{nullptr};
    std::string prefix{};
};

/** A class handle. Accumulates the `@field`/`@operator` lines (which sit *inside*
    the `---@class` block) and the method/constructor statements (which follow it),
    then flushes the assembled block to the document on destruction — the driver
    has no explicit "finish class" hook, so RAII is the finalizer. Move-only so a
    moved-from temporary does not double-flush. */
struct class_writer {
    document* doc{nullptr};
    std::string qualified{};  /**< Dotted LuaCATS class name. */
    std::string cls_doc{};    /**< The class `doc`. */
    std::string bases{};      /**< Comma-joined base names, or empty. */
    std::string fields{};     /**< Accumulated `---@field` / `---@operator` lines. */
    std::string methods{};    /**< Accumulated `function` statements. */
    std::vector<func_overload> ctors{}; /**< The `.new` overloads, grouped on flush. */

    class_writer() = default;
    class_writer(const class_writer&) = delete;
    class_writer& operator=(const class_writer&) = delete;
    class_writer(class_writer&& o) noexcept { *this = std::move(o); }
    class_writer& operator=(class_writer&& o) noexcept {
        doc = o.doc;
        qualified = std::move(o.qualified);
        cls_doc = std::move(o.cls_doc);
        bases = std::move(o.bases);
        fields = std::move(o.fields);
        methods = std::move(o.methods);
        ctors = std::move(o.ctors);
        o.doc = nullptr; // the source no longer flushes
        return *this;
    }
    ~class_writer() {
        if (!doc)
            return;
        emit_doc_comment(doc->body, cls_doc.empty() ? nullptr : cls_doc.c_str());
        doc->body += "---@class " + qualified;
        if (!bases.empty())
            doc->body += " : " + bases;
        doc->body += '\n';
        doc->body += fields;
        doc->body += qualified + " = {}\n\n";
        // Constructors (all producing `Class.new`) are grouped into one documented
        // function with `---@overload` signatures; they precede the methods.
        if (!ctors.empty())
            render_overload_group(doc->body, qualified + ".new", ctors);
        doc->body += methods;
    }
};

/** An enum handle: the value table text accumulated by `add_enumerator`, flushed
    as a `---@enum` block by RAII (same rationale as @ref class_writer). */
struct enum_writer {
    document* doc{nullptr};
    std::string qualified{};
    std::string enum_doc{};
    std::string values{}; /**< Accumulated `    Name = value,` lines. */

    enum_writer() = default;
    enum_writer(const enum_writer&) = delete;
    enum_writer& operator=(const enum_writer&) = delete;
    enum_writer(enum_writer&& o) noexcept { *this = std::move(o); }
    enum_writer& operator=(enum_writer&& o) noexcept {
        doc = o.doc;
        qualified = std::move(o.qualified);
        enum_doc = std::move(o.enum_doc);
        values = std::move(o.values);
        o.doc = nullptr;
        return *this;
    }
    ~enum_writer() {
        if (!doc)
            return;
        emit_doc_comment(doc->body, enum_doc.empty() ? nullptr : enum_doc.c_str());
        doc->body += "---@enum " + qualified + '\n';
        doc->body += qualified + " = {\n" + values + "}\n\n";
    }
};

} // namespace welder::rods::luacats
