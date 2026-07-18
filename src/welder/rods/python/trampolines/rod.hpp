#pragma once
/** @file
    welder **trampoline-generator** rod (header-only, text-emitting).

    A Python subclass can only override a welded type's `virtual` methods when the type
    is bound with a *trampoline* — a hand-written C++ subclass that captures each
    virtual call and forwards it to Python (see the guide's inheritance chapter and
    `<welder/rods/python/trampoline.hpp>`). welder cannot synthesize that subclass as a
    live class (P2996 has no member-declaration injection), but it *can* emit one as
    **source text** and let the consuming translation unit compile it. This rod does
    exactly that: it reflects a welded namespace and writes a `.hpp` of ready-to-compile
    trampolines — the Python analogue of the LuaCATS stub rod, and it plugs the *same*
    generic driver (`<welder/welder.hpp>`) so type discovery, `weld`/policy resolution
    and inheritance handling are reused verbatim.

    The generated trampolines are **backend-neutral** — they use welder's neutral
    `WELDER_PY_TRAMPOLINE` / `WELDER_PY_OVERRIDE` macros, so one generated header
    compiles under either Python rod (pybind11 or nanobind); the consuming TU includes
    the active backend's `trampoline.hpp` first. Each override splices the base
    virtual's own reflected return/parameter types, so signatures match by construction
    (see `<welder/rods/python/trampolines/document.hpp>` for the rendering core and its
    one limitation, C-variadic virtuals).

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`). Drive it
    by hand:
    @code
    #include <welder/rods/python/trampolines/rod.hpp>
    welder::rods::trampolines::rod::generate<^^mymod>(std::cout);
    @endcode
    or through the generator-`main()` macro (`module.hpp`):
    @code
    #include <welder/rods/python/trampolines/module.hpp>
    WELDER_TRAMPOLINES_MAIN(mymod)   // main(): print the trampoline header for ^^mymod
    @endcode
*/

#include <cstddef>
#include <meta>
#include <ostream>
#include <string>
#include <utility>

#include <welder/welder.hpp>                              // driver + rod contract
#include <welder/rods/python/trampolines/document.hpp>    // the emission core

namespace welder::inline v0::rods::trampolines {

/** The trampoline-generator rod: a stateless @ref welder::rod that, instead of
    registering a live module, appends a compilable trampoline `struct` (plus its
    `trampoline_for` registration) for every welded *virtual* type it visits.

    Only @ref make_class does work — it emits the trampoline for a type carrying
    overridable virtuals (skipping a whole-type `bind_flat`). Every other primitive is
    a no-op: members, constructors, enums, free functions and variables contribute no
    trampolines. */
struct rod {
    static constexpr lang language{lang::py}; /**< Trampolines are a Python concept. */

    /** A copyable handle onto the growing @ref document that the driver threads through
        every emission primitive (the rod's `module_type`). @a prefix carries the dotted
        module path for parity with the other rods; the generator keys entirely off each
        type's own reflected namespace, so it is unused here. */
    struct module_handle {
        document* doc{};
        std::string prefix{};
    };
    using module_type = module_handle;

    /** The class handle the driver threads to the per-member hooks. The generator emits a
        whole trampoline from `make_class` alone, so the per-member hooks are no-ops and
        this carries nothing. */
    struct class_handle {};

    /** The enum handle. Enums have no virtuals, so nothing is emitted for them. */
    struct enum_handle {};

    /** The class / enum handles the per-class / per-enum hooks receive — exactly what
        `make_class` / `make_enum` return (a generator handle carries no type
        parameter). Named as associated types so the @ref welder::rod concept can
        shape-check the per-handle hooks against them. */
    template <class> using class_handle_type = class_handle;
    template <class> using enum_handle_type = enum_handle;

    struct session {}; /**< No deferred module state. */

    /** Permissive: the generator only reproduces virtual *signatures* (via splices,
        which work for any type), so it does not need the backend's bindability oracle —
        disabling the gate lets it run over the same types the Python rods bind without
        re-deriving their caster logic. @see welder::caster_oracle */
    template <class T>
    static constexpr bool has_native_caster = true;

    /** No operators are emitted (they are covered as virtuals when overridable), so
        none are "special". @see welder::rod */
    static consteval const char* special_method_name(std::meta::info) {
        return nullptr;
    }

    // --- class binding: the one primitive that emits ------------------------

    /** Emit @a T's trampoline iff it has overridable virtuals and is not a whole-type
        `bind_flat`. @a Bases / @a name / @a doc are unused (a trampoline derives from
        @a T alone and needs no target-language name). @see welder::rod */
    template <class T, auto Bases, std::size_t... I>
    static class_handle make_class(module_type& m, const char* /*name*/,
                                   const char* /*doc*/, std::index_sequence<I...>) {
        if constexpr (!::welder::rods::python::overridable_virtuals(^^T).empty() &&
                      !::welder::rods::python::bound_flat(^^T))
            m.doc->template add<^^T>();
        return {};
    }

    /** The declaring-entity-aware form the carriage prefers (see `bind_type`): @a
        Decl is the *spelling* of @a T — `^^T` for a directly-declared class, or the
        namespace-scope **alias** through which a class-template specialization was
        welded. The generated text derives everything from @a Decl (base clause,
        `trampoline_for` key, slot re-derivations), which is what lets it name a
        specialization at all: `Ring<int>` has no identifier, `::ns::IntRing` does.
        `bind_flat` is read through the dealiased type (marks live on the template). */
    template <class T, std::meta::info Decl, auto Bases, std::size_t... I>
    static class_handle make_class(module_type& m, const char* /*name*/,
                                   const char* /*doc*/, std::index_sequence<I...>) {
        if constexpr (!::welder::rods::python::overridable_virtuals(Decl).empty() &&
                      !::welder::rods::python::bound_flat(std::meta::dealias(Decl)))
            m.doc->template add<Decl>();
        return {};
    }

    template <class T, auto Ctors, bool HasDefault, bool Aggregate, bool Copyable>
    static void add_constructors(class_handle&) {}
    template <std::meta::info, class = ::welder::naming::none>
    static void add_field(class_handle&) {}
    template <auto Fns, class = ::welder::naming::none>
    static void add_method(class_handle&) {}
    template <auto Fns, class = ::welder::naming::none>
    static void add_static_method(class_handle&) {}
    template <class T, auto Fns> static void add_operator(class_handle&) {}
    template <class T, auto Fns, auto Covered>
    static void add_comparisons(class_handle&) {}
    template <class T, std::meta::info Fn>
    static void add_stringifier(class_handle&) {}

    // --- enum binding: nothing to emit --------------------------------------

    template <class E>
    static enum_handle make_enum(module_type&, const char*, const char*) {
        return {};
    }
    template <std::meta::info, class = ::welder::naming::none>
    static void add_enumerator(enum_handle&) {}
    template <class E> static void finish_enum(enum_handle&) {}

    // --- namespace / module binding: threading only -------------------------

    static session open_module(module_type&) { return {}; }
    static void set_module_doc(module_type&, const char*) {}
    template <auto Fns, class = ::welder::naming::none>
    static void add_function(module_type&, const char* = nullptr) {}
    template <std::meta::info, class = ::welder::naming::none>
    static void add_variable(module_type&, session&, const char* = nullptr) {}

    /** Recurse into a nested namespace, carrying the document handle through.
        @see welder::rod */
    static module_type add_submodule(module_type& m, const char* name) {
        return module_type{m.doc, m.prefix.empty() ? std::string{name}
                                                   : m.prefix + "." + name};
    }
    static void close_module(module_type&, session&) {}

    // --- whole-header generation (this backend's extra entry point) ---------

    /** Emit the trampoline header for the welded types in namespace @a Ns to @a os.

        Runs welder's generic driver over @a Ns with this text-emitting backend, so the
        header carries a trampoline for exactly the welded virtual types the Python rods
        bind — inherited virtuals covered, `bind_flat` honoured. Style is accepted for
        signature parity with the other rods but unused (a trampoline needs no
        target-language names).
        @tparam Ns a reflection of the (top-level) namespace to scan.
        @param os the stream to write the finished header to. */
    template <std::meta::info Ns, class Style = ::welder::naming::none>
    static void generate(std::ostream& os) {
        static_assert(std::meta::is_namespace(Ns),
                      "welder: trampolines::generate<Ns>: Ns must reflect a namespace");
        document doc{};
        module_type m{&doc, {}};
        ::welder::welder<rod, Style>::template weld_namespace<Ns>(m);
        os << doc.render();
    }
};

static_assert(::welder::rod<rod>,
              "welder::rods::trampolines::rod must satisfy welder::rod");

} // namespace welder::rods::trampolines