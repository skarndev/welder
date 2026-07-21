#pragma once
/** @file
    welder **opaque-container generator** rod (header-only, text-emitting).

    A build-time rod that reflects the welded types in a namespace, finds the STL
    containers they use (`std::vector`/`std::map`/`std::unordered_map` — see
    `<welder/containers.hpp>`), and emits a `.hpp` that binds them **opaque / by
    reference**: the `WELDER_OPAQUE(...)` declarations (global scope) plus welded
    aliases (in the namespace), so the user never hand-writes that boilerplate. It is
    the exact model of the trampoline / LuaCATS generators — a real @ref welder::rod
    that plugs the *same* driver but emits source text instead of registering a live
    module, so member selection, base flattening and marks are reused verbatim.

    Every reference container a welded type uses is opened by reference by default
    (the trampoline generator's zero-opt-in philosophy — all of them); opt a data
    member's container type *out* with `[[=welder::rods::python::by_value]]`
    (`marks.hpp`). Names are derived (`std::vector<int>` → `VectorInt`).

    Use `WELDER_OPAQUE_CONTAINERS_MAIN(ns)` (this directory's `module.hpp`) for the
    generator `main()`, and `welder_generate_opaque_containers()` (CMake) to build and
    run it. The consuming TU includes the active backend's `rod.hpp` (which defines
    `WELDER_OPAQUE`), then the generated header, before welding the namespace.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`). */

#include <array>
#include <cstddef>
#include <meta>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/welder.hpp>
#include <welder/rods/python/opaque_containers/document.hpp>
#include <welder/rods/python/opaque_containers/marks.hpp> // marked_by_value

namespace welder::inline v0::rods::opaque_containers {

/** The opaque-container generator rod (satisfies @ref welder::rod). Its emission
    primitives COLLECT the reference containers each welded surface uses into a
    @ref document; `generate` renders the header. */
struct rod {
    static constexpr lang language{lang::py}; /**< Opaque containers are Python-only. */

    /** A copyable handle onto the growing @ref document threaded through every
        emission primitive (the rod's `module_type`). @a prefix carries the dotted
        module path for parity with the other rods; unused (aliases render into the
        generate-time namespace). */
    struct module_handle {
        document* doc{};
        std::string prefix{};
    };
    using module_type = module_handle;

    /** The per-class handle: it carries the @ref document so the per-member hooks
        (`add_field` / `add_method` / …) can collect from members and signatures. */
    struct class_handle {
        document* doc{};
    };

    /** The enum handle — enums use no containers, so it carries nothing. */
    struct enum_handle {};

    template <class> using class_handle_type = class_handle;
    template <class> using enum_handle_type = enum_handle;

    struct session {}; /**< No deferred module state. */

    /** Permissive: the generator only *reads* the welded surface to find containers,
        so it does not need a backend's bindability oracle — disabling the gate lets
        it run over the same types the Python rods bind without re-deriving their
        caster logic. @see welder::caster_oracle */
    template <class T>
    static constexpr bool has_native_caster = true;

    /** No operators expose special names here (containers are collected from operator
        signatures directly). @see welder::rod */
    static consteval const char* special_method_name(std::meta::info) {
        return nullptr;
    }

    // --- class binding: pass the document down to the per-member hooks -------

    template <class T, auto Bases, std::size_t... I>
    static class_handle make_class(module_type& m, const char* /*name*/,
                                   const char* /*doc*/, std::index_sequence<I...>) {
        return {m.doc};
    }
    /** The declaring-entity-aware form the carriage prefers (see `bind_type`); the
        generator keys off member types, so @a Decl is unused. */
    template <class T, std::meta::info Decl, auto Bases, std::size_t... I>
    static class_handle make_class(module_type& m, const char* /*name*/,
                                   const char* /*doc*/, std::index_sequence<I...>) {
        return {m.doc};
    }
    template <class T, auto Bases, std::size_t... I>
    static class_handle make_nested_class(module_type& m, class_handle&,
                                          const char* /*name*/, const char* /*doc*/,
                                          std::index_sequence<I...>) {
        return {m.doc};
    }

    /** Collect from each participating constructor's parameter types. @see welder::rod */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate, bool Copyable>
    static void add_constructors(class_handle& cls) {
        template for (constexpr auto ctor : std::define_static_array(Ctors))
            cls.doc->template collect_callable<ctor>();
    }

    /** Collect the container(s) in data member @a Mem's type — excluded when @a Mem
        carries `[[=welder::rods::python::by_value]]`. @see welder::rod */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(class_handle& cls) {
        cls.doc->template collect<std::meta::type_of(Mem)>(
            ::welder::rods::python::marked_by_value(Mem));
    }

    /** Collect from the property accessor signatures (getter return, setter param).
        @see welder::rod */
    template <class T, std::meta::info Getter, std::meta::info Setter>
    static void add_property(class_handle& cls, const char*) {
        cls.doc->template collect_callable<Getter>();
        if constexpr (Setter != std::meta::info{})
            cls.doc->template collect_callable<Setter>();
    }

    template <auto Fns, class Style = ::welder::naming::none>
    static void add_method(class_handle& cls) {
        template for (constexpr auto fn : std::define_static_array(Fns))
            cls.doc->template collect_callable<fn>();
    }
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_static_method(class_handle& cls) {
        template for (constexpr auto fn : std::define_static_array(Fns))
            cls.doc->template collect_callable<fn>();
    }
    template <class T, auto Fns>
    static void add_operator(class_handle& cls) {
        template for (constexpr auto fn : std::define_static_array(Fns))
            cls.doc->template collect_callable<fn>();
    }
    template <class T, auto Fns, auto Covered>
    static void add_comparisons(class_handle&) {} // spaceship operands are the anchor type
    template <class T, std::meta::info Fn>
    static void add_stringifier(class_handle&) {} // ostream inserter: no container surface

    // --- enum binding: nothing to collect -----------------------------------

    template <class E>
    static enum_handle make_enum(module_type&, const char*,
                                 const ::welder::detail::enum_doc&) {
        return {};
    }
    template <class E>
    static enum_handle make_nested_enum(module_type&, class_handle&, const char*,
                                        const ::welder::detail::enum_doc&) {
        return {};
    }
    template <std::meta::info, class = ::welder::naming::none>
    static void add_enumerator(enum_handle&) {}
    template <class E> static void finish_enum(enum_handle&) {}

    // --- namespace / module binding -----------------------------------------

    static session open_module(module_type&) { return {}; }
    static void set_module_doc(module_type&, const char*) {}

    /** Collect from a free function's overload group signatures. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_function(module_type& m, const char* = nullptr) {
        template for (constexpr auto fn : std::define_static_array(Fns))
            m.doc->template collect_callable<fn>();
    }

    /** Collect the container(s) in a namespace variable's type. @see welder::rod */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, session&, const char* = nullptr) {
        m.doc->template collect<std::meta::type_of(Var)>(false);
    }

    /** Recurse into a nested namespace, carrying the document through. @see welder::rod */
    static module_type add_submodule(module_type& m, const char* name) {
        return module_type{m.doc, m.prefix.empty() ? std::string{name}
                                                   : m.prefix + "." + name};
    }
    static void close_module(module_type&, session&) {}

    // --- whole-header generation (this backend's extra entry point) ----------

    /** Emit the opaque-container header for the welded types in namespace @a Ns to
        @a os. Runs welder's generic driver over @a Ns with this text-emitting
        backend, so the header opens exactly the containers the Python rods would bind
        — `by_value`-marked ones excluded. Style is accepted for signature parity but
        unused (container names are derived, not styled).
        @tparam Ns a reflection of the (top-level) namespace to scan.
        @param os the stream to write the finished header to. */
    template <std::meta::info Ns, class Style = ::welder::naming::none>
    static void generate(std::ostream& os) {
        static_assert(std::meta::is_namespace(Ns),
                      "welder: opaque_containers::generate<Ns>: Ns must reflect a "
                      "namespace");
        document doc{};
        module_type m{&doc, {}};
        ::welder::welder<rod, Style>::template weld_namespace<Ns>(m);
        // namespace_name is consteval (heap std::string), so materialize its result
        // to a static C-string before it crosses into this runtime call.
        os << doc.render(std::define_static_string(namespace_name(Ns)));
    }
};

static_assert(::welder::rod<rod>,
              "welder::rods::opaque_containers::rod must satisfy welder::rod");

} // namespace welder::inline v0::rods::opaque_containers
