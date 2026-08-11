#pragma once
/** @file
    welder C#/.NET backend (header-only, text-emitting) — the rod itself.

    C# has no in-process "register a class into a live module" C API the way CPython
    and Lua do, so this backend cannot register at C++ load time like the pybind11 /
    sol2 rods. Instead it takes the idiomatic, **cross-platform** interop path — a C
    ABI shared library driven from managed code by P/Invoke — and, like the LuaCATS
    stub rod, emits it as *text* at build time. Uniquely, it emits **two coordinated
    artifacts** from one driver pass:

    - `shim.cpp` — `extern "C"` thunks compiled **with reflection enabled** against
      the same welded header, each a one-liner delegating into the compiled
      marshalling library (`<welder/rods/csharp/shim_support.hpp>`) parameterized by
      the exact member reflection — the trampolines rod's splice-don't-respell idiom
      applied to the C ABI, so no C++ type is ever respelled as text;
    - `Bindings.cs` — `[LibraryImport]` P/Invoke declarations plus idiomatic wrapper
      classes (a per-class `SafeHandle`, `IDisposable`, properties, natural C#
      overloads, `enum : <underlying>`), calling those thunks.

    Both are written together per emission primitive, keyed by the same symbol, so
    the native and managed sides cannot desync (a colliding symbol is a designed
    generator error). Every thunk carries a trailing `welder_error*` out-param: a
    C++ exception is caught in the marshalling layer and rethrown managed-side as
    `WelderNativeException` — it never unwinds through the C ABI.

    This plugs the *same* generic driver (`<welder/welder.hpp>`) the other rods use,
    so member selection, overload grouping, policy / mark resolution and the
    bindability gate are reused verbatim — only the emission differs.

    **What lives where.** This file is the rod: the hook table the driver calls,
    each hook a one-line forward into a focused emission layer. The layers are:

    | Layer | Include |
    |---|---|
    | the two emitted artifacts + the writer handles | `<welder/rods/csharp/document.hpp>` |
    | classification, spellings, lookups, symbols | `<welder/rods/csharp/type_map.hpp>` |
    | entity references and placeholders | `<welder/rods/csharp/emit/refs.hpp>` |
    | the three per-type spellings | `<welder/rods/csharp/emit/spellings.hpp>` |
    | parameters and the managed return path | `<welder/rods/csharp/emit/params.hpp>`, `<welder/rods/csharp/emit/returns.hpp>` |
    | the thunk+P/Invoke+wrapper triple | `<welder/rods/csharp/emit/callables.hpp>` |
    | classes, constructors, fields, methods, operators | `<welder/rods/csharp/emit/classes.hpp>` … |
    | generated container wrappers | `<welder/rods/csharp/emit/containers.hpp>` |
    | the director apparatus (C# overrides a C++ virtual) | `<welder/rods/csharp/emit/directors.hpp>` |
    | the compiled marshalling library the shim calls | `<welder/rods/csharp/shim_support.hpp>` |

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`).
    Drive it through the whole-module generator:
    @code
    #include <welder/rods/csharp/module.hpp>
    WELDER_CSHARP_MAIN(mymod, "mymod.hpp", "mymod_native")
    @endcode
*/

#include <cstddef>
#include <meta>
#include <ostream>
#include <string>
#include <utility>

#include <welder/welder.hpp>                      // welder::welder + rod contract
#include <welder/doc.hpp>                         // doc_of
#include <welder/naming.hpp>                      // name_of + ent_kind
#include <welder/rods/csharp/document.hpp>        // the two-artifact document
#include <welder/rods/csharp/emit/classes.hpp>    // open/finish a class or enum
#include <welder/rods/csharp/emit/constructors.hpp>
#include <welder/rods/csharp/emit/containers.hpp>
#include <welder/rods/csharp/emit/fields.hpp>
#include <welder/rods/csharp/emit/methods.hpp>
#include <welder/rods/csharp/emit/namespaces.hpp>
#include <welder/rods/csharp/emit/operators.hpp>
#include <welder/rods/csharp/naming.hpp>          // the dotnet name style
#include <welder/rods/csharp/type_map.hpp>

namespace welder::inline v0::rods::csharp {

/** The C#/.NET rod: a stateless policy satisfying @ref welder::rod that emits a
    native shim + a managed P/Invoke wrapper instead of registering a live module.

    Every hook is a one-line forward into `<welder/rods/csharp/emit/…>`; the rod
    exists to *be* the driver's contract, not to hold the emission logic. A rod
    that wants to specialize one step can therefore derive from this and override
    that hook alone. */
struct rod {
    static constexpr lang language{lang::cs};
    using module_type = module_writer;

    /** The class / enum handles the per-class / per-enum hooks write into —
        exactly what `make_class` / `make_enum` return. Named as associated types
        so the @ref welder::rod concept can shape-check the per-handle hooks. */
    template <class> using class_handle_type = class_writer;
    template <class> using enum_handle_type = enum_writer;

    struct session {}; /**< No deferred module state. */

    /** @ref is_native_dotnet drives the shared bindability gate. */
    template <class T>
    static constexpr bool has_native_caster = is_native_dotnet<std::remove_cvref_t<T>>;

    /** The CLS metadata name (`op_Addition`, …) for a mapped operator, or
        `nullptr` for the unmapped ones (assignment, compound assignment,
        `++`/`--`, dereference, …) — the driver's eligibility gate.
        @param op_fn a reflection of the operator function.
        @return the CLS name, or `nullptr` when unmapped. */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return cs_operator(op_fn).cls_name;
    }

    // --- class binding ------------------------------------------------------

    /** Open welded class @a T's binding. @see welder::rods::csharp::open_class */
    template <class T, auto Bases, std::size_t... I>
    static class_writer make_class(module_type& m, const char* name,
                                   const char* doc, std::index_sequence<I...>) {
        return open_class<T, ^^T, Bases>(m, name, doc);
    }

    /** The declaring-entity-aware form the carriage prefers: @a Decl is `^^T`,
        or the namespace-scope **alias** a class-template specialization was
        welded through — the one C++-spellable anchor such a target has, which
        the emitted shim's `^^…` spellings need.
        @see welder::rods::csharp::open_class */
    template <class T, std::meta::info Decl, auto Bases, std::size_t... I>
    static class_writer make_class(module_type& m, const char* name,
                                   const char* doc, std::index_sequence<I...>) {
        return open_class<T, Decl, Bases>(m, name, doc);
    }

    /** Place a NESTED member class under its enclosing type's binding.
        @see welder::rods::csharp::open_nested_class */
    template <class T, auto Bases, std::size_t... I>
    static class_writer make_nested_class(module_type& m, class_writer& outer,
                                          const char* name, const char* doc,
                                          std::index_sequence<I...>) {
        return open_nested_class<T, ^^T, Bases>(m, outer, name, doc);
    }

    /** The declaring-entity-aware nested form the carriage prefers.
        @see welder::rods::csharp::open_nested_class */
    template <class T, std::meta::info Decl, auto Bases, std::size_t... I>
    static class_writer make_nested_class(module_type& m, class_writer& outer,
                                          const char* name, const char* doc,
                                          std::index_sequence<I...>) {
        return open_nested_class<T, Decl, Bases>(m, outer, name, doc);
    }

    /** Place a NESTED member enum under the outer's binding (same model).
        @see welder::rods::csharp::open_nested_enum */
    template <class E>
    static enum_writer make_nested_enum(module_type& m, class_writer& outer,
                                        const char* name, const char* doc) {
        return open_nested_enum<E>(m, outer, name, doc);
    }

    /** Nothing to finalize — the nested writer's RAII flush lands its block
        inside the outer's members before the outer itself flushes. */
    template <class T>
    static void finish_nested_class(module_type&, class_writer&, class_writer&,
                                    const char*) {}

    /** Emit the whole constructor surface in one call (main's contract).
        @see welder::rods::csharp::emit_constructors */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate, bool Copyable>
    static void add_constructors(class_writer& w) {
        emit_constructors<T, Ctors, HasDefault, Aggregate, Copyable>(w);
    }

    /** Bind data member @a Mem as a C# property.
        @see welder::rods::csharp::emit_field */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(class_writer& w) {
        emit_field<Mem, Style>(w);
    }

    /** Bind a resolved accessor pair as a C# property.
        @see welder::rods::csharp::emit_property */
    template <class T, std::meta::info Getter, std::meta::info Setter>
    static void add_property(class_writer& w, const char* name) {
        emit_property<T, Getter, Setter>(w, name);
    }

    /** Bind method overload group @a Fns as natural C# overloads.
        @see welder::rods::csharp::emit_method_group */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_method(class_writer& w) {
        emit_method_group<Fns, Style>(w);
    }

    /** Bind static-method overload group @a Fns as `public static` overloads.
        @see welder::rods::csharp::emit_static_method_group */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_static_method(class_writer& w) {
        emit_static_method_group<Fns, Style>(w);
    }

    /** Emit operator slot group @a Fns — member and anchored free entries
        mixed, each with its own indexed symbol. Arithmetic/bitwise/unary map
        to `public static` C# operators (a reflected free entry — @a T on the
        right — emits with its declared operand order, which C# permits as
        long as one operand is the class); comparisons route through the class
        writer's pairing ledger (see `document.hpp`); `operator[]` becomes a
        get-only indexer; `operator()` an `Invoke` method. */
    template <class T, auto Fns>
    static void add_operator(class_writer& w) {
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            collect_containers<fn>(*w.doc);
            emit_operator<T, fn>(w);
        }
    }

    /** Synthesize the C# relational operators from `operator<=>` group @a Fns:
        one wire thunk per overload collapses the ordering to an int
        (`shim::compare` evaluates `l <=> r` through C++'s own rewriting rules,
        heterogeneous operands included), and the four relational operators
        derive from it — minus the slots an explicit participating operator
        already @a Covered. A heterogeneous overload also emits the reversed
        operand order (C++'s rewritten `5 < obj` works, so C#'s should). `==`
        is never synthesized (mirroring C++: only `operator==` rewrites it —
        a defaulted spaceship's implicit `==` arrives via the operator path). */
    template <class T, auto Fns, auto Covered>
    static void add_comparisons(class_writer& w) {
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            emit_comparison_set<T, fn, Covered>(w);
        }
    }

    /** Bind the swept free ostream inserter @a Fn as `ToString()`.
        @see welder::rods::csharp::emit_stringifier */
    template <class T, std::meta::info Fn>
    static void add_stringifier(class_writer& w) {
        emit_stringifier<T, Fn>(w);
    }

    // --- enum binding -------------------------------------------------------

    /** Open welded enum @a E's binding. @see welder::rods::csharp::open_enum */
    template <class E>
    static enum_writer make_enum(module_type& m, const char* name,
                                 const char* doc) {
        return open_enum<E>(m, name, doc);
    }

    /** Emit a `Name = value,` line for enumerator @a Enum, preceded by its
        `[[=welder::doc]]` as a `<summary>` — C# has the per-member doc slot
        Python lacks, so nothing folds into the enum's summary. */
    template <std::meta::info Enum, class Style = ::welder::naming::none>
    static void add_enumerator(enum_writer& w) {
        emit_doc_comment(w.values, "        ", ::welder::doc_of<Enum>());
        w.values += "        ";
        w.values += ::welder::name_of<Enum, lang::cs, Style,
                                      ::welder::ent_kind::enumerator>();
        w.values += " = ";
        constexpr long long v{static_cast<long long>(std::to_underlying([:Enum:]))};
        w.values += std::to_string(v);
        w.values += ",\n";
    }

    /** Nothing to finalize — the enum writer's RAII flush lands its block. */
    template <class E>
    static void finish_enum(enum_writer&) {}

    // --- namespace / module binding -----------------------------------------

    /** No deferred module state to open. */
    static session open_module(module_type&) { return {}; }

    /** The root namespace's doc becomes the `Bindings.cs` file-header comment
        (C# has no namespace doc slot); first caller wins — the root is swept
        first.
        @param m   the module handle.
        @param doc the namespace's doc text. */
    static void set_module_doc(module_type& m, const char* doc) {
        if (doc && *doc && m.doc->module_doc.empty())
            m.doc->module_doc = doc;
    }

    /** Emit free-function overload group @a Fns onto the namespace's `Global`
        static class. @see welder::rods::csharp::emit_function_group */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_function(module_type& m, const char* name = nullptr) {
        emit_function_group<Fns, Style>(m, name);
    }

    /** Emit namespace variable @a Var as a static property.
        @see welder::rods::csharp::emit_variable */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, session&, const char* name = nullptr) {
        emit_variable<Var, Style>(m, name);
    }

    /** A nested C++ namespace = a REAL nested C# namespace: extend the dotted
        path; the submodule's types and `Global` members land there.
        @param m    the enclosing module handle.
        @param name the nested namespace's C# name.
        @return the submodule handle. */
    static module_type add_submodule(module_type& m, const char* name) {
        return module_type{m.doc, m.cs_ns.empty() ? std::string{name}
                                                  : m.cs_ns + "." + name};
    }

    /** Nothing to close — the document is rendered by @ref generate. */
    static void close_module(module_type&, session&) {}

    // --- whole-module generation (this backend's extra entry point) ---------

    /** Emit the C# wrapper (@a cs) and native shim (@a shim) for namespace @a Ns.

        Runs welder's generic driver over @a Ns with this text-emitting backend, so
        the two artifacts cover exactly what would be bound at runtime — classes,
        enums, free functions, namespace variables and nested namespaces.
        @tparam Ns    a reflection of the (top-level) namespace / module.
        @tparam Style the C# name style (defaults to @ref dotnet).
        @param shim the stream for `shim.cpp`.
        @param cs   the stream for `Bindings.cs`.
        @param o    the module knobs (C# namespace, P/Invoke library, shim include). */
    template <std::meta::info Ns, class Style = dotnet>
    static void generate(std::ostream& shim, std::ostream& cs, options o) {
        static_assert(std::meta::is_namespace(Ns),
                      "welder: csharp::generate<Ns>: Ns must reflect a namespace");
        document doc{};
        if (o.cs_namespace.empty())
            o.cs_namespace = std::define_static_string(std::meta::identifier_of(Ns));
        doc.opts = std::move(o);
        // Every nested C++ namespace becomes a REAL nested C# namespace; each
        // namespace's free functions / variables land in its `Global` static
        // class (C# has no namespace-scope functions), its types beside it.
        module_writer m{&doc, ""};
        ::welder::welder<rod, Style>::template weld_namespace<Ns>(m);
        shim << doc.render_shim();
        cs << doc.render_cs();
    }
};

static_assert(::welder::rod<rod>,
              "welder::rods::csharp::rod must satisfy welder::rod");

} // namespace welder::inline v0::rods::csharp
