#pragma once
#include <meta>
#include <type_traits>

#include <welder/carriage.hpp> // the traversal driver + stitch/tack carriage aliases
#include <welder/concepts.hpp> // the welder::rod contract
#include <welder/naming.hpp>   // naming::none (the default name style)

/** @file
    welder's binding entry point: the @ref welder::welder struct.

    welder's core walks a reflected type/namespace and decides *what* to bind
    (`bind_traits.hpp`) and *whether each type can be represented* (`bindable.hpp`);
    the reflection-driven traversal that orchestrates it — the **carriage** — lives
    in `<welder/carriage.hpp>`, and the **rod** contract every backend satisfies is
    the @ref welder::rod concept in `<welder/concepts.hpp>`. Everything
    language-specific — *how* to register a class, method, property or module
    attribute — is delegated to a **rod** (a welding rod: the backend that lays down
    the bindings): a stateless policy type (`welder::rods::pybind11::rod<>`,
    `…::nanobind::rod`, `…::sol2::rod`) supplying a fixed set of emission primitives.

    The public face of all of it is `welder::welder<Rod>` here: one struct,
    parameterized on the rod, whose static members run the reflection-driven binding
    at whichever stage of the usual hand-binding flow you want to automate (a single
    type, a namespace into an existing module, a namespace as a fresh submodule, or a
    whole module), each a one-line forward to the carriage.

    Provide the vocabulary first — `#include <welder/vocabulary.hpp>` — then this
    header (each backend header includes it for you).
*/

namespace welder {

/** welder's binding entry point, parameterized on a **rod**.

    One struct is all a user drives: pick a rod (e.g. `welder::rods::pybind11::rod<>`,
    from that rod's header) and call the static member matching the stage of the
    usual hand-binding flow you want to automate — welder generates the
    backend-agnostic boilerplate there, and everything around it stays ordinary
    hand-written binding code.

    @code
    #include <welder/rods/python/pybind11/rod.hpp>
    using weld = welder::welder<welder::rods::pybind11::rod<>>;

    PYBIND11_MODULE(mymod, m) {
        weld::weld_type<MyType>(m);          // one type onto an existing module
        weld::weld_function<^^my_free_fn>(m);// one free function onto m
        weld::weld_variable<^^my_global>(m); // one global/constant onto m
        weld::weld_namespace<^^myns>(m);     // a namespace's members into m
        weld::weld_namespace_as_submodule<^^other>(m); // …or into m.other
    }
    @endcode

    (For zero hand-written entry-point code at all, each rod also ships a
    `module.hpp` with the `WELDER_MODULE` entry-point macro.)

    **Composition / subclassing.** Every entry point is a one-line forward to the
    injected @ref welder::carriage "Carriage" (the traversal driver), which owns the
    resolution and the welded/bindability gates. There are two ways to go beyond the
    stock flow, both without re-deriving any of that: **inject** a different carriage
    as the third template argument (e.g. @ref welder::tack_welding_carriage to bind an
    unmarked library), or **subclass** `welder::welder` — the whole class is static and
    non-virtual, so deriving simply gives a user's own driver type access to the bound
    Rod / Style / Carriage (via `rod_type` / `name_style` / `carriage_type`) and to the
    public entry points, to assemble bespoke routines (a hand-picked subset of a
    namespace, welded and hand-written registrations interleaved) from the carriage's
    gated building blocks.

    @tparam B     the rod to emit through (any type satisfying @ref welder::rod).
    @tparam Style the name style every generated name flows through — a class,
                  method, field, … is renamed into the target language's convention
                  (see `<welder/naming.hpp>`). A `[[=welder::weld_as]]` override on an
                  entity beats the style and is used verbatim (and a call-site `name`
                  argument beats even that). Defaults to @ref welder::naming::none (bind
                  C++ identifiers unchanged); a Python binding might pass
                  `welder::rods::python::pep8`.
    @tparam Carriage the reflection-driven traversal driver (@ref welder::carriage) —
                  what walks a type/namespace and orchestrates the rod's emission.
                  Defaults to @ref welder::stitch_welding_carriage (marker-directed);
                  inject @ref welder::tack_welding_carriage to bind an unmarked library
                  greedily, or any `basic_carriage<Resolution>`.
*/
template <rod B, class Style = naming::none, class Carriage = carriage>
struct welder {
    /** The rod this instantiation emits through. */
    using rod_type = B;
    /** The name style this instantiation renames through. */
    using name_style = Style;
    /** The traversal driver (carriage) this instantiation drives through. */
    using carriage_type = Carriage;
    /** The rod's module handle (`py::module_`, `sol::table`, …). */
    using module_type = typename B::module_type;

    /** A do-nothing module hook; the default for weld_module()'s pre/post. */
    static constexpr auto noop{[](module_type&) {}};

    /** Reflect over @a T and register it on module @a m.

        A class type binds with its constructors, fields, methods, operators and
        native bases; an enum with its enumerators (see the carriage for the full set).
        Under the default (stitch) carriage @a T must be welded for `B::language`.

        @tparam T the class or enum type to bind.
        @param m    the module handle to register onto.
        @param name the target-language name (verbatim, beats `weld_as`), or `nullptr`
                    to resolve @a T's `weld_as`/styled name.
        @return the rod's class/enum handle, for chaining further
                registrations. */
    template <class T>
    static auto weld_type(module_type& m, const char* name = nullptr) {
        if constexpr (std::is_enum_v<T>)
            return Carriage::template bind_enum<B, T, Style>(m, name);
        else
            return Carriage::template bind_type<B, T, Style>(m, name);
    }

    /** Reflect over free function @a Fn and register it on module @a m.

        The semi-manual analogue of what namespace binding does for a namespace's
        free functions: bind one hand-picked function directly, without welding the
        whole enclosing namespace. @a Fn must reflect a single function (an overload
        set is ambiguous — reflect the specific overload you want); under the default
        (stitch) carriage it must be welded for `B::language`.

        @tparam Fn a reflection of the free function.
        @param m    the module handle to register onto.
        @param name the target name, used **verbatim** and taking precedence over any
                    `[[=welder::weld_as]]` on @a Fn; `nullptr` (default) resolves the
                    `weld_as`/styled name. */
    template <std::meta::info Fn>
    static void weld_function(module_type& m, const char* name = nullptr) {
        Carriage::template bind_function<B, Fn, Style>(m, name);
    }

    /** Reflect over global/namespace variable @a Var and register it on module @a m.

        The semi-manual analogue of what namespace binding does for a namespace's
        variables: expose one hand-picked global or constant directly. A
        const/constexpr @a Var becomes a value snapshot; a mutable one a live get/set
        property over the C++ global. Under the default (stitch) carriage @a Var must
        be welded for `B::language`.

        @tparam Var a reflection of the namespace-scope variable.
        @param m    the module handle to register onto.
        @param name the target name, used **verbatim** and taking precedence over any
                    `[[=welder::weld_as]]` on @a Var; `nullptr` (default) resolves the
                    `weld_as`/styled name. */
    template <std::meta::info Var>
    static void weld_variable(module_type& m, const char* name = nullptr) {
        Carriage::template bind_variable<B, Var, Style>(m, name);
    }

    /** Reflect over namespace @a Ns and expose its members on module @a m.

        Classes and enums bind via weld_type(), free functions and namespace
        variables become module attributes, and a nested namespace holding
        participating content becomes a submodule. Which members participate is the
        carriage's call (marker-directed by default, greedy under the tack carriage).

        @tparam Ns a reflection of the namespace.
        @param m the module handle to fill.
        @return @a m, for chaining. */
    template <std::meta::info Ns>
    static module_type& weld_namespace(module_type& m) {
        Carriage::template bind_namespace<B, Ns, Style>(m);
        return m;
    }

    /** Define a submodule of @a m (via the backend) and weld_namespace() @a Ns
        into it.

        @tparam Ns a reflection of the namespace.
        @param m    the parent module handle.
        @param name the submodule name (verbatim), or `nullptr` to resolve @a Ns's
                    styled/`weld_as` name.
        @return the new submodule handle, for chaining. */
    template <std::meta::info Ns>
    static module_type weld_namespace_as_submodule(module_type& m,
                                                   const char* name = nullptr) {
        return Carriage::template bind_namespace_as_submodule<B, Ns, Style>(m, name);
    }

    /** Build a whole module out of top-level namespace @a Ns: run @a pre, weld the
        namespace into @a m (adopting a namespace-level `doc` as the module
        docstring), run @a post.

        The hooks fold hand-written bindings in around welder's generated body.
        This fills an *existing* module handle; pair it with an entry-point macro
        (the framework's own, or the backend's `WELDER_MODULE` expansion, which
        calls this).

        @tparam Ns   a reflection of the top-level namespace (its name is meant to
                     be the module name).
        @tparam Pre  the pre-hook callable type.
        @tparam Post the post-hook callable type.
        @param m    the module handle to fill.
        @param pre  invoked with @a m before binding (defaults to noop).
        @param post invoked with @a m after binding (defaults to noop).
        @return @a m, for chaining. */
    template <std::meta::info Ns, class Pre = decltype(noop),
              class Post = decltype(noop)>
    static module_type& weld_module(module_type& m, Pre pre = noop,
                                    Post post = noop) {
        Carriage::template build_module<B, Ns, Style>(m, pre, post);
        return m;
    }
};

} // namespace welder
