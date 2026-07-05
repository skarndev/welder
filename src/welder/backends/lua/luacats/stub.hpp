#pragma once
/** @file
    welder LuaCATS stub backend (header-only, text-emitting).

    Lua has no runtime docstring slot, so the sol2 backend drops every
    `[[=welder::doc]]` / `returns` / parameter annotation at load time — a loaded
    sol2 usertype exposes nothing an introspecting tool could scrape back. This
    backend is their home: it reflects the *same* welded types and emits a
    [LuaCATS](https://luals.github.io/wiki/annotations/) **`---@meta` definition
    file** (`.lua`), the analogue of the Python backends' `.pyi` stubs, carrying the
    class/field/function types *and* the docstrings for editor completion and the
    Lua language server.

    Unlike Python's `.pyi` (pybind11-stubgen *loads* the built module and
    introspects it), a Lua stub cannot be scraped from a loaded module — so it is
    **reflection-emitted at build time**. This is a genuine welder @ref
    welder::backend "backend": it plugs the *same* generic driver
    (`<welder/backend.hpp>`) that pybind11/nanobind/sol2 use, so member selection,
    base-class flattening, policy/mark resolution and the bindability gate are
    reused verbatim. Only the emission primitives differ — instead of registering a
    live class they append LuaCATS text to a growing document. A tiny generator
    executable runs the driver in `main()` and prints the document; a CMake helper
    (`welder_luacats_generate_stub`) captures it to `<name>.lua`.

    Requires the welder vocabulary first (via `import welder;` or `#include
    <welder/welder.hpp>`). Then:
    @code
    #include <welder/backends/lua/luacats/stub.hpp>
    WELDER_LUACATS_MAIN(mymod)   // main(): print the ---@meta stub for namespace ^^mymod
    @endcode
    or drive it by hand:
    @code
    welder::luacats::generate<^^mymod>(std::cout);
    @endcode

    ## The type map (the one thing sol2 did not need)

    The runtime sol2 backend only needed a yes/no *caster oracle*; a stub needs the
    actual LuaCATS **type name** for every C++ type. @ref detail::lua_type_string
    maps scalars (`integer`/`number`/`boolean`/`string`), the STL wrappers welder
    recurses (`std::vector<T>` → `T[]`, `std::map<K,V>` → `table<K,V>`,
    `std::optional<T>` → `T?`, smart pointers → the pointee), and welded
    classes/enums to their dotted Lua name; anything unrecognized degrades to `any`.
*/

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>  // WELDER_LUACATS_MAIN: write the stub to an output-path argument
#include <iostream> // WELDER_LUACATS_MAIN: default to stdout
#include <meta>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <welder/backend.hpp>     // the backend contract + generic driver
#include <welder/bind_traits.hpp> // param_types / aggregate_fields
#include <welder/doc.hpp>         // doc_of / param_docs / return_doc_of
#include <welder/module.hpp>      // WELDER_MODULE dispatch (unused entry, kept parallel)

namespace welder::luacats {

namespace detail {

// --- small text helpers -----------------------------------------------------

/** Append @a text as `--- ` comment lines (each source line prefixed), so a
    multiline summary lands as a LuaCATS description block. A null/empty @a text
    emits nothing. */
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
    text collapse to spaces (LuaCATS tags are single-line). Empty if @a text is
    null. */
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
    contribute nothing. */
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
    `^^std::is_integral_v`) on the type @a t. */
consteval bool type_trait(std::meta::info trait_var, std::meta::info t) {
    return std::meta::extract<bool>(std::meta::substitute(trait_var, {t}));
}

/** Whether @a type (already a bare type) is a listed element-wise wrapper whose
    LuaCATS spelling this map renders; @a tmpl_out receives its class template. */
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
    type reflection. */
consteval const char* lua_type(std::meta::info type) {
    return std::define_static_string(lua_type_string(type));
}

/** Whether the backend converts @a U without welder registering a type: scalars,
    strings and `char*`. Classes and enums are program-defined, so they must be
    welded — mirrors the sol2 oracle without depending on sol2. */
template <class U>
inline constexpr bool is_native_lua =
    std::is_arithmetic_v<U> ||
    std::is_same_v<std::remove_cv_t<U>, std::string> ||
    std::is_same_v<std::remove_cv_t<U>, std::string_view> ||
    std::is_same_v<std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<U>>>,
                   char>;

// --- signature rendering ----------------------------------------------------

/** The LuaCATS type name of each parameter of @a Fn, as a splice-ready array
    parallel to `param_docs<Fn>()`. */
template <std::meta::info Fn>
consteval auto param_lua_types() {
    // Guard n != 0: param_types<Fn> materializes std::array<info, n> and indexes
    // it, and std::array<T, 0>::operator[] is not usable (as the rest of welder
    // guards too), so it must not be instantiated for a parameterless function.
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    std::array<const char*, n> out{};
    if constexpr (n != 0) {
        constexpr auto types{welder::detail::param_types<Fn>()};
        for (std::size_t i{0}; i < n; ++i)
            out[i] = std::define_static_string(lua_type_string(types[i]));
    }
    return out;
}

/** The comma-joined argument-name list for @a Fn's declaration line (an unnamed
    parameter becomes `arg<N>`). */
template <std::meta::info Fn>
std::string arg_list() {
    static constexpr auto pds{welder::param_docs<Fn>()};
    std::string out{};
    for (std::size_t i{0}; i < pds.size(); ++i) {
        if (i)
            out += ", ";
        out += pds[i].name ? std::string{pds[i].name}
                           : "arg" + std::to_string(i + 1);
    }
    return out;
}

/** Emit the `---@param` lines for @a Fn (name, LuaCATS type, and its `doc`). */
template <std::meta::info Fn>
void emit_params(std::string& out) {
    static constexpr auto pds{welder::param_docs<Fn>()};
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

/** Emit a full documented function statement: summary, `@param`s, `@return`, then
    `function <callee>(<args>) end`.

    A non-empty @a ret_override forces the return type — used for a constructor,
    whose reflection carries no return type (so `return_type_of` must not be
    instantiated for it: the `is_constructor` guard discards that branch). */
template <std::meta::info Fn>
void emit_function(std::string& out, const std::string& callee,
                   const std::string& ret_override = {}) {
    emit_doc_comment(out, welder::doc_of<Fn>());
    emit_params<Fn>(out);
    if (!ret_override.empty())
        out += "---@return " + ret_override + '\n';
    else if constexpr (!std::meta::is_constructor(Fn)) {
        using R = [:std::meta::return_type_of(Fn):];
        if constexpr (!std::is_void_v<R>) {
            out += "---@return ";
            out += lua_type(std::meta::return_type_of(Fn));
            const std::string d{one_line(welder::return_doc_of<Fn>())};
            if (!d.empty())
                out += ' ' + d;
            out += '\n';
        }
    }
    out += "function " + callee + "(" + arg_list<Fn>() + ") end\n\n";
}

// --- the backend document ---------------------------------------------------

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

    /** Record (once) a module table to declare, updating its doc if given. */
    void declare_table(std::string_view prefix, const char* doc = nullptr) {
        for (auto& t : tables)
            if (t.prefix == prefix) {
                if (doc && *doc)
                    t.doc = doc;
                return;
            }
        tables.push_back({std::string{prefix}, doc ? doc : ""});
    }

    /** The finished stub text: the `---@meta` header, the module tables shallowest
        first, then the accumulated declarations. */
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
        return out;
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

/** The comma-joined LuaCATS names of a class's welded bases (for `---@class X : …`). */
template <auto Bases, std::size_t... I>
consteval const char* bases_string(std::index_sequence<I...>) {
    std::string out{};
    ((out += (out.empty() ? "" : ", ") + qualified_name(Bases[I])), ...);
    return std::define_static_string(out);
}

/** Precompute an aggregate's `---@param` lines and its `.new` argument list.

    A flat function template (not an immediately-invoked generic lambda): a
    splice + constant-index pack expansion inside such a lambda misbehaves on
    gcc-16, so the field names/types are materialized into constant arrays by the
    pack, then consumed by a plain runtime loop. The driver only calls this for an
    aggregate with at least one field, so the pack is never empty. */
template <class T, std::size_t... J>
void aggregate_param_lines(std::string& out, std::string& args,
                           std::index_sequence<J...>) {
    static constexpr auto fields{welder::detail::aggregate_fields<T>()};
    static constexpr const char* names[]{
        std::define_static_string(std::meta::identifier_of(fields[J]))...};
    static constexpr const char* types[]{
        lua_type(std::meta::type_of(fields[J]))...};
    for (std::size_t i{0}; i < sizeof...(J); ++i) {
        out += "---@param ";
        out += names[i];
        out += ' ';
        out += types[i];
        out += '\n';
        args += (args.empty() ? "" : ", ");
        args += names[i];
    }
}

/** A member operator's LuaCATS `---@operator` name, or `nullptr` if not rendered.

    LuaCATS names the arithmetic/comparison/call/index metamethods; unary vs binary
    is by arity (a member operator takes 0 parameters when unary). Kept deliberately
    close to the sol2 backend's metamethod map so the stub and the runtime binding
    agree on which operators surface. */
consteval const char* operator_luacats(std::meta::info f) {
    using std::meta::operators;
    const bool unary{welder::detail::is_unary_operator(f)};
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:   return unary ? nullptr : "add";
        case operators::op_minus:  return unary ? "unm" : "sub";
        case operators::op_star:   return unary ? nullptr : "mul";
        case operators::op_slash:  return "div";
        case operators::op_percent: return "mod";
        case operators::op_equals_equals: return "eq";
        case operators::op_less:          return "lt";
        case operators::op_less_equals:   return "le";
        case operators::op_parentheses:   return "call";
        case operators::op_square_brackets: return "index";
        default: return nullptr; // ~=, >, >= are derived; others unmapped
    }
}

/** The LuaCATS stub backend: a stateless policy satisfying @ref welder::backend
    that emits text instead of registering a live module. */
struct backend {
    static constexpr lang language{lang::lua}; /**< Stubs are for the Lua binding. */
    using module_type = module_writer;

    struct session {}; /**< No deferred module state. */

    /** @ref is_native_lua drives the shared bindability gate. */
    template <class T>
    static constexpr bool has_native_caster =
        is_native_lua<std::remove_cvref_t<T>>;

    /** Member operator → its `---@operator` name, or `nullptr` (also gates
        eligibility, like every backend). */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return operator_luacats(op_fn);
    }

    // --- class binding ------------------------------------------------------

    template <class T, auto Bases, std::size_t... I>
    static class_writer make_class(module_type& m, const char* name,
                                   const char* doc, std::index_sequence<I...> seq) {
        class_writer w{};
        w.doc = m.doc;
        w.qualified = m.prefix.empty() ? std::string{name}
                                       : m.prefix + "." + name;
        w.cls_doc = doc ? doc : "";
        w.bases = bases_string<Bases>(seq);
        return w;
    }

    static void add_default_ctor(class_writer& w) {
        // Documented `Class.new()` with no arguments, returning the class.
        w.methods += "---@return " + w.qualified + '\n';
        w.methods += "function " + w.qualified + ".new() end\n\n";
    }

    template <std::meta::info Ctor>
    static void add_constructor(class_writer& w) {
        emit_function<Ctor>(w.methods, w.qualified + ".new", w.qualified);
    }

    template <class T>
    static void add_aggregate_constructor(class_writer& w) {
        // C++26 aggregate field constructor: one `.new` param per field.
        static constexpr auto fields{welder::detail::aggregate_fields<T>()};
        std::string out{}, args{};
        aggregate_param_lines<T>(out, args,
                                 std::make_index_sequence<fields.size()>{});
        out += "---@return " + w.qualified + '\n';
        out += "function " + w.qualified + ".new(" + args + ") end\n\n";
        w.methods += out;
    }

    template <std::meta::info Mem>
    static void add_field(class_writer& w) {
        w.fields += "---@field ";
        w.fields += std::meta::identifier_of(Mem);
        w.fields += ' ';
        w.fields += lua_type(std::meta::type_of(Mem));
        const std::string d{one_line(welder::doc_of<Mem>())};
        if (!d.empty())
            w.fields += ' ' + d;
        w.fields += '\n';
    }

    template <std::meta::info Fn>
    static void add_method(class_writer& w) {
        // A method is `Class:name(...)` (implicit self); reflection's parameters
        // already exclude self, so the arg list renders directly.
        emit_function<Fn>(
            w.methods,
            w.qualified + ":" + std::string{std::meta::identifier_of(Fn)});
    }

    template <std::meta::info Fn>
    static void add_static_method(class_writer& w) {
        emit_function<Fn>(
            w.methods,
            w.qualified + "." + std::string{std::meta::identifier_of(Fn)});
    }

    template <std::meta::info Fn>
    static void add_operator(class_writer& w) {
        // LuaCATS records operators on the class block: `---@operator name(rhs): R`.
        const char* op{operator_luacats(Fn)};
        w.fields += "---@operator ";
        w.fields += op;
        if constexpr (!welder::detail::is_unary_operator(Fn)) {
            constexpr auto types{param_lua_types<Fn>()};
            w.fields += "(";
            w.fields += types[0];
            w.fields += ")";
        }
        using R = [:std::meta::return_type_of(Fn):];
        if constexpr (!std::is_void_v<R>) {
            w.fields += ": ";
            w.fields += lua_type(std::meta::return_type_of(Fn));
        }
        w.fields += '\n';
    }

    // --- enum binding -------------------------------------------------------

    template <class E>
    static enum_writer make_enum(module_type& m, const char* name,
                                 const char* doc) {
        enum_writer w{};
        w.doc = m.doc;
        w.qualified = m.prefix.empty() ? std::string{name}
                                       : m.prefix + "." + name;
        w.enum_doc = doc ? doc : "";
        return w;
    }

    template <std::meta::info Enum>
    static void add_enumerator(enum_writer& w) {
        w.values += "    ";
        w.values += std::meta::identifier_of(Enum);
        w.values += " = ";
        constexpr long long v{static_cast<long long>(std::to_underlying([:Enum:]))};
        w.values += std::to_string(v);
        w.values += ",\n";
    }

    template <class E>
    static void finish_enum(enum_writer&) {} // RAII flush handles it

    // --- namespace / module binding -----------------------------------------

    static session open_module(module_type&) { return {}; }

    static void set_module_doc(module_type& m, const char* doc) {
        m.doc->declare_table(m.prefix, doc);
    }

    template <std::meta::info Fn>
    static void add_function(module_type& m) {
        emit_function<Fn>(
            m.doc->body,
            (m.prefix.empty() ? std::string{}
                              : m.prefix + ".") +
                std::string{std::meta::identifier_of(Fn)});
    }

    template <std::meta::info Var>
    static void add_variable(module_type& m, session&) {
        std::string& out{m.doc->body};
        emit_doc_comment(out, welder::doc_of<Var>());
        out += "---@type ";
        out += lua_type(std::meta::type_of(Var));
        out += '\n';
        out += (m.prefix.empty() ? std::string{} : m.prefix + ".") +
               std::string{std::meta::identifier_of(Var)} + " = nil\n\n";
    }

    static module_type add_submodule(module_type& m, const char* name) {
        const std::string prefix{m.prefix.empty() ? std::string{name}
                                                  : m.prefix + "." + name};
        m.doc->declare_table(prefix);
        return module_type{m.doc, prefix};
    }

    static void close_module(module_type&, session&) {}
};

} // namespace detail

/** Emit the LuaCATS `---@meta` stub for top-level namespace @a Ns to @a os.

    Runs welder's generic driver over @a Ns with the text-emitting backend, so the
    stub covers exactly what the sol2 backend binds — classes, enums, free
    functions, namespace variables and nested namespaces — now carrying the types
    and docstrings Lua drops at runtime.

    @tparam Ns a reflection of the (top-level) namespace whose name is the module.
    @param os the stream to write the finished stub to. */
template <std::meta::info Ns>
void generate(std::ostream& os) {
    static_assert(std::meta::is_namespace(Ns),
                  "welder: luacats::generate<Ns>: Ns must reflect a namespace");
    detail::document doc{};
    detail::module_writer m{&doc,
                            std::define_static_string(detail::qualified_name(Ns))};
    doc.declare_table(m.prefix, welder::doc_of<Ns>());
    welder::detail::bind_namespace_driver<detail::backend, Ns>(m);
    os << doc.render();
}

} // namespace welder::luacats

/** @def WELDER_LUACATS_MAIN
    Define a `main()` that emits the LuaCATS stub for namespace @a ns.

    Writes to the file named by the first command-line argument, or to stdout when
    none is given. The build-time analogue of a backend entry point: a generator
    executable links this and `welder_luacats_generate_stub` runs it with the
    output path to produce `<ns>.lua`.
    @param ns the top-level namespace / module name token. */
#define WELDER_LUACATS_MAIN(ns)                                                   \
    int main(int argc, char** argv) {                                             \
        if (argc > 1) {                                                           \
            ::std::ofstream welder_out_{argv[1]};                                 \
            ::welder::luacats::generate<^^ns>(welder_out_);                       \
        } else {                                                                  \
            ::welder::luacats::generate<^^ns>(::std::cout);                       \
        }                                                                         \
        return 0;                                                                 \
    }
