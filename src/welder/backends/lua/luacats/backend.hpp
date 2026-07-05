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

    The emission primitives are thin: the C++→LuaCATS **type map** and the small
    text helpers live in `<welder/backends/lua/luacats/type_map.hpp>`, and the
    document assembler (signature rendering + the `*_writer` handle types the
    driver's module/class/enum handles deduce to) in
    `<welder/backends/lua/luacats/document.hpp>`. The backend struct below only wires
    those to the driver contract.

    Requires the welder vocabulary first (via `import welder;` or `#include
    <welder/welder.hpp>`). Then:
    @code
    #include <welder/backends/lua/luacats/backend.hpp>
    WELDER_LUACATS_MAIN(mymod)   // main(): print the ---@meta stub for namespace ^^mymod
    @endcode
    or drive it by hand:
    @code
    welder::luacats::generate<^^mymod>(std::cout);
    @endcode
*/

#include <cstddef>
#include <fstream>  // WELDER_LUACATS_MAIN: write the stub to an output-path argument
#include <iostream> // WELDER_LUACATS_MAIN: default to stdout
#include <meta>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <welder/backend.hpp>          // the backend contract + generic driver
#include <welder/backends/lua/luacats/document.hpp> // the document + writer handles
#include <welder/backends/lua/luacats/type_map.hpp> // lua_type / operator map / text
#include <welder/backends/lua/overloads.hpp> // shared Lua overload-set selectors
#include <welder/bind_traits.hpp>      // aggregate_fields / is_unary_operator
#include <welder/doc.hpp>              // doc_of / param_docs / return_doc_of
#include <welder/module.hpp>           // WELDER_MODULE dispatch (unused entry, kept parallel)

namespace welder::luacats {

namespace detail {

// The overload-set selectors are shared with the sol2 backend
// (`<welder/backends/lua/overloads.hpp>`; both gather a name's C++ overloads that the
// generic driver visits one at a time). The LuaCATS stub renders the group as one
// documented `function` plus `---@overload` lines.
using welder::lua::function_overload_set;
using welder::lua::is_overload_leader;
using welder::lua::method_overload_set;
using welder::lua::overload_group;

/** The LuaCATS stub backend: a stateless policy satisfying @ref welder::backend
    that emits text instead of registering a live module.

    Its public static members are the emission primitives welder's driver calls;
    they wire the type map (`type_map.hpp`) and document assembler (`document.hpp`)
    to the driver contract. The `protected` members below are the few helpers that
    are purely internal to the struct (prefixed `_`). */
struct backend {
    static constexpr lang language{lang::lua}; /**< Stubs are for the Lua binding. */
    using module_type = module_writer;

    struct session {}; /**< No deferred module state. */

  protected:
    // --- implementation helpers (not part of the welder::backend contract) --

    /** The comma-joined LuaCATS names of a class's welded bases (for
        `---@class X : …`). */
    template <auto Bases, std::size_t... I>
    static consteval const char* _bases_string(std::index_sequence<I...>) {
        std::string out{};
        ((out += (out.empty() ? "" : ", ") + qualified_name(Bases[I])), ...);
        return std::define_static_string(out);
    }

    /** Precompute an aggregate's `---@param` lines and its `.new` argument list.

        A flat function template (not an immediately-invoked generic lambda): a
        splice + constant-index pack expansion inside such a lambda misbehaves on
        gcc-16, so the field names/types are materialized into constant arrays by the
        pack, then consumed by a plain runtime loop. The driver only calls this for
        an aggregate with at least one field, so the pack is never empty. */
    template <class T, std::size_t... J>
    static void _aggregate_param_lines(std::string& out, std::string& args,
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

    /** The `<field>: <type>, …` list for an aggregate's `.new` `fun(…)` signature
        (its `---@overload` line). Same field source as @ref _aggregate_param_lines. */
    template <class T, std::size_t... J>
    static std::string _aggregate_fun_params(std::index_sequence<J...>) {
        static constexpr auto fields{welder::detail::aggregate_fields<T>()};
        static constexpr const char* names[]{
            std::define_static_string(std::meta::identifier_of(fields[J]))...};
        static constexpr const char* types[]{
            lua_type(std::meta::type_of(fields[J]))...};
        std::string s{};
        for (std::size_t i{0}; i < sizeof...(J); ++i) {
            if (i)
                s += ", ";
            s += names[i];
            s += ": ";
            s += types[i];
        }
        return s;
    }

  public:
    // --- caster oracle + emission primitives (the welder::backend contract) --

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
        w.bases = _bases_string<Bases>(seq);
        return w;
    }

    static void add_default_ctor(class_writer& w) {
        // A no-argument `.new()` overload returning the class; grouped on flush.
        func_overload o{};
        o.ret_line = "---@return " + w.qualified + '\n';
        o.fun_sig = "fun(): " + w.qualified;
        w.ctors.push_back(std::move(o));
    }

    template <std::meta::info Ctor>
    static void add_constructor(class_writer& w) {
        w.ctors.push_back(build_overload<Ctor>(w.qualified));
    }

    template <class T>
    static void add_aggregate_constructor(class_writer& w) {
        // C++26 aggregate field constructor: one `.new` param per field.
        static constexpr auto seq{
            std::make_index_sequence<welder::detail::aggregate_fields<T>().size()>{}};
        func_overload o{};
        _aggregate_param_lines<T>(o.params, o.args, seq);
        o.ret_line = "---@return " + w.qualified + '\n';
        o.fun_sig = "fun(" + _aggregate_fun_params<T>(seq) + "): " + w.qualified;
        w.ctors.push_back(std::move(o));
    }

    template <std::meta::info Mem>
    static void add_field(class_writer& w) {
        w.fields += "---@field ";
        w.fields += std::meta::identifier_of(Mem);
        w.fields += ' ';
        w.fields += lua_type(std::meta::type_of(Mem));
        std::string d{one_line(welder::doc_of<Mem>())};
        // LuaCATS has no read-only/const field modifier (an open feature request on
        // lua-language-server), so a const member's immutability — which the sol2
        // runtime backend enforces with sol::readonly — is surfaced as a description
        // note rather than an (unrecognized) tag.
        if constexpr (std::meta::is_const_type(std::meta::type_of(Mem)))
            d = d.empty() ? "(read-only)" : d + " (read-only)";
        if (!d.empty())
            w.fields += ' ' + d;
        w.fields += '\n';
    }

    template <std::meta::info Fn>
    static void add_method(class_writer& w) {
        // A method is `Class:name(...)` (implicit self); reflection's parameters
        // already exclude self, so the arg list renders directly. Overloads are
        // gathered into one documented function with `---@overload` signatures, so
        // the whole group is emitted once, on its first (declaration-order) member.
        if constexpr (is_overload_leader<method_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<method_overload_set, Fn, lang::lua>()};
            std::vector<func_overload> sigs{};
            collect_overloads<grp>(sigs, std::make_index_sequence<grp.size()>{});
            render_overload_group(
                w.methods,
                w.qualified + ":" + std::string{std::meta::identifier_of(Fn)}, sigs);
        }
    }

    template <std::meta::info Fn>
    static void add_static_method(class_writer& w) {
        if constexpr (is_overload_leader<method_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<method_overload_set, Fn, lang::lua>()};
            std::vector<func_overload> sigs{};
            collect_overloads<grp>(sigs, std::make_index_sequence<grp.size()>{});
            render_overload_group(
                w.methods,
                w.qualified + "." + std::string{std::meta::identifier_of(Fn)}, sigs);
        }
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
        // Free-function overloads group just like methods (see add_method).
        if constexpr (is_overload_leader<function_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<function_overload_set, Fn, lang::lua>()};
            std::vector<func_overload> sigs{};
            collect_overloads<grp>(sigs, std::make_index_sequence<grp.size()>{});
            render_overload_group(
                m.doc->body,
                (m.prefix.empty() ? std::string{} : m.prefix + ".") +
                    std::string{std::meta::identifier_of(Fn)},
                sigs);
        }
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

static_assert(welder::backend<backend>,
              "welder::luacats::detail::backend must satisfy welder::backend");

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
