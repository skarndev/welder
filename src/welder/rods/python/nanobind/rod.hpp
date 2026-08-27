#pragma once
/** @file
    welder nanobind rod (header-only).

    This is a *thin* rod: it implements welder's rod contract
    (`<welder/welder.hpp>`) for nanobind and hands the traversal/resolution off to
    welder's generic driver. All the language-agnostic work — deciding which
    members bind, gating bindability, folding docstrings, walking bases and
    namespaces — lives in the core; only the nanobind emission primitives are here.
    It is a near-mirror of the pybind11 backend (same class-handle model), differing
    only where nanobind's API does: `def_rw`/`def_ro` instead of
    `def_readwrite`/`def_readonly`, a placement-`__init__` factory, module
    docstrings set through `__doc__`, and the `is_base_caster` bindability probe.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`).
    This header exposes exactly one thing: the rod template
    `welder::rods::nanobind::rod<DocStyle = google_style>`, to plug into
    `welder::welder` (`rod<>` for the default Google docstring style;
    `rod<numpy_style>` / `rod<sphinx_style>` for the other dialects):
    @code
    #include <nanobind/nanobind.h>
    #include <welder/rods/python/nanobind/rod.hpp>
    NB_MODULE(mymod, m) {
        welder::welder<welder::rods::nanobind::rod<>>::weld_type<MyType>(m);
    }
    @endcode
    (For the `WELDER_MODULE` entry-point macro, include this directory's
    `module.hpp` instead.)

    @note nanobind supports only *single* inheritance at the binding level (one C++
    base per `nb::class_`). welder's driver may hand this backend more than one
    welded base (e.g. a multiple-inheritance diamond); those cases bind under
    pybind11 but not nanobind, and trip nanobind's own single-base static assertion.
*/
#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/welder.hpp>     // welder::welder + the rod contract + driver
#include <welder/rods/python/doc_style.hpp> // welder::rods::python::google_style
#include <welder/rods/python/operators.hpp> // welder::rods::python::operator_dunder
#include <welder/rods/python/trampoline.hpp> // trampoline_for / gate / coverage
#include <welder/bind_traits.hpp>// param_types / param_names / aggregate_fields
#include <welder/doc.hpp>        // function_docstring

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>            // nb::ndarray (zero-copy numpy views)
#include <nanobind/stl/bind_vector.h>    // nb::bind_vector (opaque sequences)
#include <nanobind/stl/optional.h>       // lazy NSDMI defaults (Optional[F] = None)
#include <nanobind/stl/bind_map.h>       // nb::bind_map (opaque maps)

#include <welder/containers.hpp>         // container_kind_of (bind_vector vs bind_map)
#include <welder/rods/python/array_interface.hpp> // numpy __array_interface__ for POD vectors

/** Declare a container type **opaque** so nanobind binds it by reference (via
    `weld`-an-alias + `bind_vector`/`bind_map`) instead of copy-converting it through
    `<nanobind/stl/…>`. A thin, backend-neutral spelling of `NB_MAKE_OPAQUE`; place
    it at namespace scope, before the module. (nanobind also hard-errors at compile
    time if a container is `bind_vector`-bound while its stl caster is included without
    this — the error names `NB_MAKE_OPAQUE` as the fix.) In a translation unit with
    more than one Python rod, use the framework-native macros directly (this resolves
    to whichever rod header was included first). @see welder/containers.hpp */
#ifndef WELDER_OPAQUE
#define WELDER_OPAQUE(...) NB_MAKE_OPAQUE(__VA_ARGS__)
#endif

namespace welder::inline v0::rods::nanobind {

// Inside `welder::rods::nanobind`, the unqualified name `nanobind` resolves to *this*
// namespace, not the library. Alias the real one once — the way nanobind's own
// docs spell it — and use `nb::` for every library reference below.
namespace nb = ::nanobind;

/** The nanobind rod: a stateless policy type satisfying @ref welder::rod.

    Its public static members are the nanobind emission primitives welder's driver
    calls; the driver supplies all the reflection-derived decisions. Each implements
    the correspondingly-named hook of the @ref welder::rod contract (and
    @ref welder::caster_oracle) — every one carries a `@see` back to it, where the
    shared parameter and return-value semantics are documented once rather than
    repeated on each backend's mirror. The `protected` members below are
    nanobind-specific implementation helpers (prefixed `_`), not part of the contract.

    @tparam DocStyle the docstring convention this rod folds function/parameter/
                     return docs into (a @ref welder::doc_style). Defaults to
                     @ref welder::rods::python::google_style; pass
                     @ref welder::rods::python::numpy_style or
                     @ref welder::rods::python::sphinx_style to emit those dialects.
                     Defaulted, so `rod<>` is the Google-style rod and code that
                     wants a different dialect names `rod<numpy_style>`.
*/
template <::welder::doc_style DocStyle = ::welder::rods::python::google_style>
struct rod {
    static constexpr lang language{lang::py}; /**< welder::lang::py. */
    using module_type = nb::module_;          /**< nanobind's module handle. */

    /** The enum handle `make_enum` yields — exactly its return type. (The class handle
        `class_handle_type` is defined next to `make_class` below, since it tracks that
        function's return.) Named as an associated type so the @ref welder::rod concept
        can shape-check the per-enum hooks against it. */
    template <class E> using enum_handle_type = nb::enum_<E>;

  protected:
    // --- implementation helpers (not part of the welder::rod contract) --

    /** Whether nanobind can only convert @a T via *runtime class registration*.

        True iff @a T's caster is (or derives from) nanobind's generic
        `type_caster_base` fallback, which looks @a T up in nanobind's
        registered-types map (`is_base_caster_v` is nanobind's own name for "this
        caster derives from `type_caster_base`"). True for program-defined classes
        and enums; false for scalars, strings and the nanobind wrapper types
        (`nb::object`, `nb::dict`, …). This is the one bindability fact welder's core
        cannot know on its own; it drives `has_native_caster` below.

        Like the pybind11 backend's counterpart, it is *conservative* — it reads
        @a T's caster type at compile time, so it reports whether @a T *needs* a
        class/enum, never whether one exists at runtime, and "native" is relative to
        the TU's includes (`std::string` / `std::vector` / … are native only when
        their `<nanobind/stl/…>` converter header is included). A type hand-registered
        out-of-band still reads `true`; that false positive is resolved by the
        deferred `trust_bindable` escape hatch.

        **Enums are forced into the needs-registration bucket**: nanobind's
        dedicated enum caster is not the base caster, but it converts only once
        the enum is registered (`nb::enum_`) — an unregistered enum fails at call
        time. Forcing it keeps welder's gate honest (a welded enum's registration
        is required) and matches every other rod.

        @tparam T the type whose caster to classify.
    */
    template <class T>
    static constexpr bool _needs_registration =
        std::is_enum_v<std::remove_cvref_t<T>> ||
        nb::detail::is_base_caster_v<nb::detail::make_caster<T>>;

    /** Map welder's neutral @ref welder::rv_kind to nanobind's `rv_policy`.

        nanobind carries the full set, `none` included, so every kind maps.
        @param k the neutral policy.
        @return the nanobind policy. */
    static consteval nb::rv_policy _rv_policy(::welder::rv_kind k) {
        switch (k) {
        case ::welder::rv_kind::automatic:           return nb::rv_policy::automatic;
        case ::welder::rv_kind::automatic_reference: return nb::rv_policy::automatic_reference;
        case ::welder::rv_kind::take_ownership:      return nb::rv_policy::take_ownership;
        case ::welder::rv_kind::copy:                return nb::rv_policy::copy;
        case ::welder::rv_kind::move:                return nb::rv_policy::move;
        case ::welder::rv_kind::reference:           return nb::rv_policy::reference;
        case ::welder::rv_kind::reference_internal:  return nb::rv_policy::reference_internal;
        case ::welder::rv_kind::none:                return nb::rv_policy::none;
        }
        return nb::rv_policy::automatic;
    }

    /** Register the function/method reflected by @a Fn onto a nanobind target.

        The folded docstring is passed when non-empty, and `nb::arg(name)...` when
        every parameter is named (so Python callers see real keyword arguments, not
        positional-only). Two call policies ride along as trailing `.def` extras:
        the `[[=welder::return_policy]]` (mapped to `rv_policy`, always passed —
        `automatic` is nanobind's default, so an unannotated call is unchanged) and
        each `[[=welder::keep_alive]]` (spliced as `nb::keep_alive<nurse,
        patient>()`). A reference-category policy on a by-value return is rejected
        first (@ref welder::validate_return_policy).
        @tparam Fn a reflection of the function.
        @tparam Def the target-adapter callable type.
        @tparam I the parameter index pack.
        @tparam K the keep_alive-dependency index pack.
        @param name     the Python name.
        @param def_into adapts the target — `cls.def`, `cls.def_static`, or `m.def`.
    */
    /** The styled kwarg names of @a Fn's parameters: each named parameter's
        identifier reshaped through the style's FIELD hook — a keyword argument
        is attribute-shaped in the target language, so it follows the same
        convention as data members (`clientPath` C++ ⇒ `client_path` kwarg under
        a snake_case style). Unnamed parameters stay null.
        @tparam Fn a reflection of the callable.
        @return one entry per parameter, static storage. */
    template <std::meta::info Fn, class Style>
    static consteval auto _styled_param_names() {
        constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
        std::array<const char*, n> names{};
        std::size_t i{0};
        for (auto p : std::meta::parameters_of(Fn))
            names[i++] = std::meta::has_identifier(p)
                             ? std::define_static_string(Style::transform_field(p))
                             : nullptr;
        return names;
    }

    template <std::meta::info Fn, class Style, class Def, std::size_t... I, std::size_t... K>
    static void _def_function(const char* name, Def def_into,
                              std::index_sequence<I...>, std::index_sequence<K...>) {
        static constexpr auto names{_styled_param_names<Fn, Style>()};
        static constexpr auto ka{::welder::detail::keep_alive_pairs<Fn>()};
        ::welder::validate_return_policy<Fn, language>();
        constexpr nb::rv_policy rvp{_rv_policy(::welder::return_policy_of(Fn, language))};
        const std::string doc{::welder::function_docstring<Fn, DocStyle>()};
        if constexpr (::welder::detail::all_params_named<Fn>()) {
            if (doc.empty())
                def_into(name, &[:Fn:], nb::arg(names[I])..., rvp,
                         nb::keep_alive<ka[K].nurse, ka[K].patient>()...);
            else
                def_into(name, &[:Fn:], doc.c_str(), nb::arg(names[I])..., rvp,
                         nb::keep_alive<ka[K].nurse, ka[K].patient>()...);
        } else {
            if (doc.empty())
                def_into(name, &[:Fn:], rvp,
                         nb::keep_alive<ka[K].nurse, ka[K].patient>()...);
            else
                def_into(name, &[:Fn:], doc.c_str(), rvp,
                         nb::keep_alive<ka[K].nurse, ka[K].patient>()...);
        }
    }

    /** Bind ONE truncated overload of @a Fn taking its first `sizeof...(I)`
        parameters: the wrapper calls the C++ function with that many arguments
        and the LANGUAGE applies the real default(s) — reflection can see that
        a default exists but not its value, so re-stating it is impossible and
        synthesizing the call is the only faithful binding. @a Self is the
        BOUND class for a nonstatic member (the wrapper takes `Self&` and calls
        through the object, so members flattened from bases resolve by
        ordinary lookup), or `void` for free/static functions.

        The truncated defs carry the argument names (kwargs keep working) and
        the return policy; the docstring stays on the full-arity def only, and
        keep_alive annotations are deliberately not repeated — an omitted
        argument cannot nurse anything, and the full overload still carries
        them for explicit calls. */
    template <std::meta::info Fn, class Self, class Style, class Def, std::size_t... I>
    static void _def_truncated(const char* name, Def def_into,
                               std::index_sequence<I...>) {
        // maybe_unused: a zero-arity truncation has an empty I pack, so the
        // splices below never reference params and gcc flags it set-but-unused.
        [[maybe_unused]] static constexpr auto params{
            ::welder::detail::param_types<Fn>()};
        static constexpr auto names{_styled_param_names<Fn, Style>()};
        constexpr nb::rv_policy rvp{
            _rv_policy(::welder::return_policy_of(Fn, language))};
        if constexpr (std::is_void_v<Self>) {
            auto wrapper = [](typename [:params[I]:]... a) -> decltype(auto) {
                return [:Fn:](std::forward<typename [:params[I]:]>(a)...);
            };
            if constexpr (::welder::detail::all_params_named<Fn>())
                def_into(name, wrapper, nb::arg(names[I])..., rvp);
            else
                def_into(name, wrapper, rvp);
        } else {
            auto wrapper = [](Self& self,
                              typename [:params[I]:]... a) -> decltype(auto) {
                return self.[:Fn:](std::forward<typename [:params[I]:]>(a)...);
            };
            if constexpr (::welder::detail::all_params_named<Fn>())
                def_into(name, wrapper, nb::arg(names[I])..., rvp);
            else
                def_into(name, wrapper, rvp);
        }
    }

    /** Bind every omissible arity of @a Fn (one per trailing defaulted
        parameter): arities `P-D .. P-1`, the full-arity def having been bound
        by @ref _def_function already. */
    template <std::meta::info Fn, class Self, class Style, class Def, std::size_t... K>
    static void _def_default_truncations(const char* name, Def def_into,
                                         std::index_sequence<K...>) {
        constexpr std::size_t P{std::meta::parameters_of(Fn).size()};
        constexpr std::size_t D{sizeof...(K)};
        (_def_truncated<Fn, Self, Style>(name, def_into,
                                  std::make_index_sequence<P - D + K>{}),
         ...);
    }

    /** Convenience overload: derive the parameter and keep_alive index sequences
        from @a Fn. */
    template <std::meta::info Fn, class Style, class Def>
    static void _def_function(const char* name, Def def_into) {
        _def_function<Fn, Style>(
            name, def_into,
            std::make_index_sequence<std::meta::parameters_of(Fn).size()>{},
            std::make_index_sequence<
                ::welder::detail::keep_alive_pairs<Fn>().size()>{});
    }

    /** Register `nb::init<P0, P1, …>()` for constructor @a Ctor.

        Names the parameters (`nb::arg`) when all are named, otherwise positional.
        @tparam Ctor a reflection of the constructor.
        @tparam I the parameter index pack.
        @param cls the class handle.
    */
    /** Bind every omissible arity of constructor @a Ctor (arities `P-D ..
        P-1`): @ref _def_init already takes the parameter index sequence, so
        each omissible arity is just a shorter one — the init calls the
        constructor with fewer arguments and the LANGUAGE applies the real
        defaults. A named template (not an immediately-invoked lambda): a
        lambda whose body touches `std::meta::parameters_of` is escalated to an
        immediate function, and the runtime `_def_init` call inside would then
        be ill-formed. */
    template <std::meta::info Ctor, std::size_t D, class Style, std::size_t... K>
    static void _def_init_truncations(auto& cls, std::index_sequence<K...>) {
        constexpr std::size_t P{std::meta::parameters_of(Ctor).size()};
        (_def_init<Ctor, Style>(cls, std::make_index_sequence<P - D + K>{}), ...);
    }

    template <std::meta::info Ctor, class Style, std::size_t... I>
    static void _def_init(auto& cls, std::index_sequence<I...>) {
        static constexpr auto params{::welder::detail::param_types<Ctor>()};
        static constexpr auto names{_styled_param_names<Ctor, Style>()};
        if constexpr (::welder::detail::all_params_named<Ctor>())
            cls.def(nb::init<typename [:params[I]:]...>(), nb::arg(names[I])...);
        else
            cls.def(nb::init<typename [:params[I]:]...>());
    }

    /** The subclass-faithful engine behind `__copy__`/`__deepcopy__`.

        Mirrors what Python's own copy machinery does for a pure-Python object —
        state transfer, never `__init__`: an uninitialized shell of the
        instance's *dynamic* type (`type(self).__new__(type(self))`, so a Python
        subclass copies as itself), the C++ payload copy-constructed in place on
        the shell (for a Python-derived shell the *alias* (trampoline) payload —
        which is why the trampoline needs a copy-from-base constructor — so the
        copy keeps dispatching virtuals into Python), then the instance
        `__dict__` carried over. With @a memo (the `__deepcopy__` path) the fresh
        object is
        recorded under `id(self)` *before* the `__dict__` is deep-copied through
        it, so shared references dedup and reference cycles terminate, exactly
        per the `copy` module's contract. `__slots__`-declared state carries
        over too: slot names are collected by `copyreg._slotnames` — the
        stdlib's own MRO-walking collector (what pickle uses), private-name
        mangling included — so a subclass keeping its state out of `__dict__`
        still copies whole.
        @tparam T the registered type.
        @param self the instance being copied (possibly of a Python subclass).
        @param memo the `__deepcopy__` memo dict, or nullptr for `__copy__`.
        @return the new instance, same dynamic type as @a self. */
    template <class T>
    static nb::object _copy_instance(nb::handle self, nb::object* memo) {
        nb::object cls{
            nb::borrow(reinterpret_cast<PyObject*>(Py_TYPE(self.ptr())))};
        nb::object out{cls.attr("__new__")(cls)};
        if (memo)
            (*memo)[nb::int_(reinterpret_cast<std::uintptr_t>(self.ptr()))] =
                out;
        // Copy-construct the C++ payload into the fresh shell in place — the
        // alias-aware work `nb::init<const T&>` would do, done here so the copy
        // needs no Python-visible `T(other)` constructor. A Python-derived shell
        // gets the trampoline so C++ virtual calls keep dispatching into the
        // Python override; a plain instance gets T. nanobind sizes every
        // instance for the alias, so either payload fits; inst_mark_ready then
        // flags it constructed and owned, as nanobind's own init path does.
        const T& src{nb::cast<const T&>(self)};
        if (nb::detail::nb_inst_python_derived(out.ptr()))
            new (nb::inst_ptr<void>(out)) construction_type<T>(src);
        else
            new (nb::inst_ptr<void>(out)) T(src);
        nb::inst_mark_ready(out);
        if (nb::hasattr(self, "__dict__")) {
            nb::object d{self.attr("__dict__")};
            if (memo)
                d = nb::module_::import_("copy").attr("deepcopy")(d, *memo);
            out.attr("__dict__").attr("update")(d);
        }
        for (nb::handle name :
             nb::module_::import_("copyreg").attr("_slotnames")(cls)) {
            if (!nb::hasattr(self, name))
                continue; // an unassigned slot stays unassigned on the copy
            nb::object v{nb::getattr(self, name)};
            if (memo)
                v = nb::module_::import_("copy").attr("deepcopy")(v, *memo);
            nb::setattr(out, name, v);
        }
        return out;
    }

    /** Whether field @a I of aggregate @a T binds its NSDMI default LAZILY:
        true for a defaultable field whose type needs class/enum registration.
        Such a default must NOT live in the function record as a Python
        object — a bound-class default holds its type through an edge the
        garbage collector cannot traverse (a plain nanobind instance is not a
        GC object, so its implicit type reference is invisible), and CHAINS
        of such defaults (Sequence{Bounds{Box{Vector}}}) become uncollectable
        cycles that nanobind reports as leaked types/instances at interpreter
        shutdown (see nanobind's refleaks documentation). The synthesized
        constructor takes std::optional<F> = None instead and materializes
        the NSDMI value in C++ when the argument is omitted. */
    template <class T, std::size_t I>
    static consteval bool _lazy_default() {
        if constexpr (I >= ::welder::detail::aggregate_defaults_from<T>()) {
            using field_type = std::remove_const_t<
                typename [:std::meta::type_of(::welder::detail::aggregate_fields<T>()[I]):]>;
            return !has_native_caster<field_type>;
        } else {
            return false;
        }
    }

    /** The synthesized field constructor's parameter type for field @a I of
        @a T: the field type itself, or std::optional of it for a lazy
        default (see @ref _lazy_default). */
    template <class T, std::size_t I>
    using _init_param = std::conditional_t<
        _lazy_default<T, I>(),
        std::optional<std::remove_const_t<
            typename [:std::meta::type_of(::welder::detail::aggregate_fields<T>()[I]):]>>,
        std::remove_const_t<
            typename [:std::meta::type_of(::welder::detail::aggregate_fields<T>()[I]):]>>;

    /** The value brace-initializing field @a I of @a T from constructor
        argument @a arg: the argument itself for a plain parameter; for a
        lazy-default optional, the engaged value or — when disengaged — the
        field's NSDMI value read off a fresh `T{}`. Yielding VALUES (rather
        than assigning into an NSDMI-initialized instance) keeps aggregate
        semantics intact for const-qualified members. */
    template <class T, std::size_t I>
    static auto _init_value(_init_param<T, I>&& arg) {
        if constexpr (_lazy_default<T, I>()) {
            constexpr auto field =
                ::welder::detail::aggregate_fields<T>()[I];
            if (arg)
                return typename _init_param<T, I>::value_type{std::move(*arg)};
            return typename _init_param<T, I>::value_type{T{}.[:field:]};
        } else {
            return std::move(arg);
        }
    }

    /** The `nb::arg` for field @a I of aggregate @a T: named after the field
        and, for the defaultable NSDMI suffix (see
        @ref welder::detail::aggregate_defaults_from), carrying the field's
        NSDMI value — read off the value-initialized @a probe — as a real
        keyword default, so Python may omit it or skip past it by keyword.
        A LAZY default (registration-needed type, @ref _lazy_default) binds
        `= None` with an `...` signature instead — no Python default object
        exists, the C++ side materializes the NSDMI value.
        @tparam T the aggregate type.
        @tparam I the field index.
        @param probe a value-initialized instance supplying the default values
                     (unused for a required or lazy-default field). */
    template <class T, std::size_t I, class Style>
    static auto _aggregate_arg([[maybe_unused]] const T& probe) {
        static constexpr auto fields{::welder::detail::aggregate_fields<T>()};
        constexpr const char* name{::welder::name_of<fields[I], language, Style,
                                                      ::welder::ent_kind::field>()};
        if constexpr (_lazy_default<T, I>()) {
            // A registration-needed default (a welded class/enum instance)
            // has no expression-shaped repr anyway — the signature spells it
            // `...`; None (or omission) selects the NSDMI value in C++.
            return (nb::arg(name).sig("...") = nb::none());
        } else if constexpr (I >= ::welder::detail::aggregate_defaults_from<T>()) {
            return nb::arg(name) = probe.[:fields[I]:];
        } else {
            return nb::arg(name);
        }
    }

    /** Synthesize a field constructor for a baseless aggregate @a T.

        nanobind builds a custom constructor by binding an `__init__` whose first
        parameter is a pointer to the (uninitialized) instance; the body
        placement-news the aggregate and assigns the provided field values.
        Fields in the NSDMI suffix become keyword parameters with real defaults
        (@ref _aggregate_arg) — except registration-needed ones, which bind
        `Optional[F] = None` and default in C++ (@ref _lazy_default), so no
        bound-class instance ever lives in the function record.
        @tparam T the aggregate type.
        @tparam I the field index pack.
        @param cls the class handle.
    */
    template <class T, class Style, std::size_t... I>
    static void _def_aggregate_init(auto& cls, std::index_sequence<I...>) {
        static constexpr auto fields{::welder::detail::aggregate_fields<T>()};
        if constexpr (::welder::detail::aggregate_defaults_from<T>() <
                      fields.size()) {
            // The probe exists only when a default is extractable
            // (aggregate_defaults_from guarantees T{} is well-formed then) —
            // and T{} in the lambda body reuses the same guarantee.
            const T probe{};
            cls.def(
                "__init__",
                [](T* self, _init_param<T, I>... args) {
                    new (self) T{_init_value<T, I>(std::move(args))...};
                },
                _aggregate_arg<T, I, Style>(probe)...);
        } else {
            cls.def(
                "__init__",
                [](T* self, typename [:std::meta::type_of(fields[I]):]... args) {
                    new (self) T{std::move(args)...};
                },
                nb::arg(::welder::name_of<fields[I], language, Style,
                                          ::welder::ent_kind::field>())...);
        }
    }

    /** Give module @a m live get/set semantics for the names in @a props.

        Reassigns @a m's Python class to a fresh subclass of its *current* class
        carrying @a props (name → property). Python modules don't support properties
        directly, but a module's `__class__` may be swapped for a `ModuleType`
        subclass. Used only when a (sub)module exposes a mutable variable.

        Subclassing the module's current class — rather than `ModuleType` outright —
        means repeated installs onto the same handle *accumulate*: welding a standalone
        variable and then a whole namespace onto the same module each add a layer, and
        the earlier properties survive in the MRO instead of being clobbered.
        @param m     the module handle.
        @param props a dict of name → `property`.
    */
    static void _install_live_properties(nb::module_& m, nb::dict props) {
        auto builtins{nb::module_::import_("builtins")};
        auto subclass{builtins.attr("type")(
            nb::str("welder_live_module"),
            nb::make_tuple(m.attr("__class__")), props)};
        // Stamp the module's own name onto the dynamically-created class, which would
        // otherwise carry `__module__ == "_frozen_importlib"` (the module-init frame).
        // Keeps functions welded onto `m` after this swap (e.g. a `weld_function`
        // following a mutable `weld_variable`) correctly attributed in stubs.
        subclass.attr("__module__") = m.attr("__name__");
        m.attr("__class__") = subclass;
    }

    /** Construct `nb::class_<T, NativeBases...>` from a reflected base-type array.

        A non-null @a doc becomes the class docstring (nanobind treats a bare
        `const char*` extra as the doc); `nullptr` is branched out rather than
        passed.
        @note nanobind accepts at most one base here; @a Bases carrying two or more
        welded bases trips nanobind's own single-base static assertion.
        @tparam T     the class type.
        @tparam Bases the static array of native base type reflections.
        @tparam I     the base index pack.
        @param scope the registration scope — the module, or (for a nested type)
                     the enclosing class handle; nanobind accepts any handle.
        @param name  the Python class name.
        @param doc   the class docstring, or `nullptr`.
    */
    template <class T, class Trampoline, auto Bases, std::size_t... I>
    static auto _make_class(nb::handle scope, const char* name, const char* doc,
                            std::index_sequence<I...>) {
        if constexpr (std::is_void_v<Trampoline>) {
            if (doc)
                return nb::class_<T, typename [:Bases[I]:]...>(scope, name, doc);
            return nb::class_<T, typename [:Bases[I]:]...>(scope, name);
        } else {
            // The trampoline is an extra `nb::class_` template argument (nanobind
            // accepts base and trampoline in either order); Python subclasses then
            // instantiate it, so their overrides capture C++ virtual calls.
            if (doc)
                return nb::class_<T, Trampoline, typename [:Bases[I]:]...>(scope, name, doc);
            return nb::class_<T, Trampoline, typename [:Bases[I]:]...>(scope, name);
        }
    }

    /** The trampoline-aware class factory over an arbitrary registration
        @a scope — the shared body of `make_class` (scope = the module) and
        `make_nested_class` (scope = the enclosing class handle). Coverage /
        bind_flat gating as documented on `make_class`. */
    template <class T, auto Bases, std::size_t... I>
    static auto _make_class_at(nb::handle scope, const char* name,
                               const char* doc, std::index_sequence<I...> seq) {
        namespace py = ::welder::rods::python;
        if constexpr (::welder::has_virtual_methods(^^T)) {
            // Resolve the trampoline: an explicit `trampoline_for<T>` wins; otherwise
            // scan T's namespace for a `[[=trampoline]]`-annotated subclass.
            constexpr auto scanned{py::scanned_trampoline_of(^^T)};
            static_assert(
                py::trampoline_for<T> != std::meta::info{} || !scanned.ambiguous,
                "welder: more than one [[=welder::rods::python::trampoline]] class in "
                "this namespace derives from T; disambiguate by specializing "
                "welder::rods::python::trampoline_for<T>.");
            constexpr std::meta::info tramp{py::trampoline_for<T> != std::meta::info{}
                                                ? py::trampoline_for<T>
                                                : scanned.type};
            if constexpr (tramp != std::meta::info{}) {
                using Trampoline = [:tramp:];
                static_assert(
                    py::trampoline_covers(^^T, ^^Trampoline),
                    "welder: the trampoline registered for this type does not "
                    "override all of its virtual methods; every virtual needs an "
                    "override forwarding to Python (see WELDER_PY_OVERRIDE).");
                return _make_class<T, Trampoline, Bases>(scope, name, doc, seq);
            } else {
                static_assert(
                    ::welder::bound_flat(^^T),
                    "welder: this welded type has virtual methods but no trampoline "
                    "is registered, so a Python subclass could not override them. "
                    "Register one — a [[=welder::rods::python::trampoline]] subclass "
                    "in T's namespace, or a welder::rods::python::trampoline_for<T> "
                    "specialization — or annotate T with "
                    "[[=welder::bind_flat]] to bind it non-overridably.");
                return _make_class<T, void, Bases>(scope, name, doc, seq);
            }
        } else {
            return _make_class<T, void, Bases>(scope, name, doc, seq);
        }
    }

  public:
    // --- caster oracle + emission primitives (the welder::rod contract) --

    /** `caster_oracle`: @a T is convertible without welder registering a class for
        it iff nanobind does *not* fall back to runtime class registration.
        @tparam T the type to classify. @see welder::caster_oracle */
    template <class T>
    static constexpr bool has_native_caster = !_needs_registration<T>;

    /** Map a member operator to its Python dunder (`nullptr` = not exposed).
        @see welder::rod */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return ::welder::rods::python::operator_dunder(op_fn);
    }

    // --- class binding ------------------------------------------------------

    /** The type welder constructs when binding @a T — its registered trampoline if
        one exists, else @a T — so an abstract base with a trampoline stays
        constructible from a Python subclass. The driver reads this to decide default
        constructibility. @see welder::rods::python::construction_type_of */
    template <class T>
    using construction_type =
        [: ::welder::rods::python::construction_type_of<T>() :];

    /** Create the `nb::class_<T, Bases…>` handle, weaving in a trampoline when @a T
        is a welded virtual type with a registered
        `welder::rods::python::trampoline_for`.

        A type carrying virtual methods is bound *overridable* — it must register a
        trampoline (so Python subclasses can override those virtuals) or opt out with
        `[[=welder::bind_flat]]`. When a trampoline is present, its
        coverage of @a T's virtuals is checked at compile time. @see _make_class
        @see welder::rods::python::trampoline_for @see welder::rod */
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* doc,
                           std::index_sequence<I...> seq) {
        return _make_class_at<T, Bases>(m, name, doc, seq);
    }

    /** Create the `nb::class_` for a **nested** member type @a T, registered
        under its enclosing type's class handle rather than the module — Python
        then sees it as `module.Outer.Inner` (and `__qualname__` nests), exactly
        like a hand-written `nb::class_<Outer::Inner>(outer_cls, "Inner")`. Same
        trampoline weaving as `make_class`. @see welder::rod */
    template <class T, auto Bases, std::size_t... I>
    static auto make_nested_class(module_type&, auto& outer_cls, const char* name,
                                  const char* doc, std::index_sequence<I...> seq) {
        return _make_class_at<T, Bases>(outer_cls, name, doc, seq);
    }

    /** The class handle `make_class` yields for @a T — exactly its return type for a
        base-less @a T (so it captures the woven-in trampoline for a virtual @a T); the
        single welded base nanobind supports is chosen by the carriage's *resolution*,
        not by @a T, so it is appended by `make_class` and is not a function of @a T
        alone. Named as an associated type so the @ref welder::rod concept can
        shape-check the per-class hooks against it. */
    template <class T>
    using class_handle_type = decltype(make_class<T, std::array<std::meta::info, 0>{}>(
        std::declval<module_type&>(), nullptr, nullptr, std::index_sequence<>{}));

    /** Retrieve the ALREADY-registered class @a T as a fillable handle — the
        two-phase binding hook (optional; declaring it opts the rod into the driver's
        two-phase namespace sweep). The name-pre-registration phase creates the
        `nb::class_<T,…>` (so `scope.attr(name)` is it); `nb::borrow` re-wraps that
        object as the same handle type `make_class` yields, so subsequent `add_*`
        calls target the same registered type. This lets the driver register every
        type's NAME (and the opaque containers using them) BEFORE filling any member,
        so a container-typed member/signature never spells a raw C++ name in a
        docstring/stub. @see welder::rod */
    template <class T>
    static class_handle_type<T> reopen_class(module_type& scope, const char* name) {
        return nb::borrow<class_handle_type<T>>(scope.attr(name));
    }

    /** The nested-scope form of @ref reopen_class — retrieve @a T from its enclosing
        type's class handle (`outer.attr(name)`). @see welder::rod */
    template <class T>
    static class_handle_type<T> reopen_nested_class(module_type&, auto& outer,
                                                    const char* name) {
        return nb::borrow<class_handle_type<T>>(outer.attr(name));
    }

    /** Bind @a T's whole constructor set (a chained-def framework just loops it):
        the default constructor when @a HasDefault, an `nb::init<…>` per member of
        @a Ctors, and the synthesized aggregate field constructor when
        @a Aggregate.

        @a Copyable (the carriage-admitted copy constructor) becomes the copy
        protocol alone — `__copy__` and `__deepcopy__(memo)`, both
        **subclass-faithful** via @ref _copy_instance — a Python subclass
        instance copies as its own type, `__dict__` and virtual dispatch intact,
        with the C++ payload duplicated by the copy constructor (whose
        deep/shallow distinction is its own: value members duplicate, a pointer
        member copies as a pointer). It is deliberately NOT exposed as a
        `T(other)` init overload: that C++-ism is unidiomatic in Python (copying
        goes through the `copy` module) and would clash with a one-arg user
        constructor. The memo parameter is typed `object`, not
        `dict` — a bare `dict` in the generated stub fails strict mypy
        (disallow_any_generics). @see _def_init @see _def_aggregate_init
        @see welder::rod */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate, bool Copyable,
              class Style = ::welder::naming::none>
    static void add_constructors(auto& cls) {
        if constexpr (HasDefault)
            cls.def(nb::init<>());
        if constexpr (Copyable) {
            // A trampolined type must keep the copy faithful for Python
            // subclasses: _copy_instance constructs the ALIAS payload on a
            // subclass shell only if it is constructible from const T&.
            static_assert(
                std::is_constructible_v<construction_type<T>, const T&>,
                "welder: this type's trampoline lacks a copy-from-base "
                "constructor, so a copied Python-subclass instance would hold a "
                "plain base payload and silently stop dispatching virtuals into "
                "Python. The WELDER_PY_TRAMPOLINE(TRAMP, BASE) macro declares "
                "it; a hand-rolled trampoline needs 'Tramp(const Base&)' — or "
                "mark::exclude the copy constructor.");
            // The copy constructor is exposed ONLY as Python's copy protocol —
            // `copy.copy`/`copy.deepcopy` — never as a `T(other)` init overload
            // (that C++-ism is unidiomatic in Python, and would collide with a
            // one-arg user constructor). _copy_instance owns the in-place copy.
            cls.def("__copy__", [](nb::handle self) {
                return _copy_instance<T>(self, nullptr);
            });
            cls.def(
                "__deepcopy__",
                [](nb::handle self, nb::object memo) {
                    return _copy_instance<T>(self, &memo);
                },
                nb::arg("memo"));
        }
        template for (constexpr auto ctor : std::define_static_array(Ctors)) {
            _def_init<ctor, Style>(cls, std::make_index_sequence<
                                     std::meta::parameters_of(ctor).size()>{});
            // C++ default arguments on constructors: _def_init already takes
            // the parameter index sequence, so each omissible arity is just a
            // shorter sequence — nb::init<first-N> calls the constructor with
            // N arguments and the language applies the real defaults.
            constexpr std::size_t d{::welder::detail::trailing_default_count<ctor>()};
            if constexpr (d > 0)
                _def_init_truncations<ctor, d, Style>(cls,
                                               std::make_index_sequence<d>{});
        }
        if constexpr (Aggregate) {
            constexpr auto fields{::welder::detail::aggregate_fields<T>()};
            _def_aggregate_init<T, Style>(cls, std::make_index_sequence<fields.size()>{});
        }
    }

    /** Whether @a Mem can bind through the class-ERASED field path (see
        `_def_erased_field`): a public, non-bit-field member declared DIRECTLY
        in bound class @a Bound, of a type nanobind converts to an IMMUTABLE
        Python object (arithmetic, enum, or std::string — where by-value vs
        by-reference at the C++ boundary is unobservable and the caster is the
        same either way).

        The direct-declaration gate is a correctness gate, not a heuristic:
        the erased accessors add `offset_of(Mem)` — an offset relative to the
        DECLARING class — to the raw instance pointer. For a member flattened
        in from a base, the base-subobject conversion belongs to the language
        (`self.*p` on the member-pointer path), so those members keep it. */
    template <std::meta::info Mem, class Bound>
    static constexpr bool _erasable_field{[] {
        if (!std::meta::is_public(Mem) || std::meta::is_bit_field(Mem))
            return false;
        if (std::meta::parent_of(Mem) != ^^Bound)
            return false;
        using F = [:std::meta::remove_cv(std::meta::type_of(Mem)):];
        if (std::meta::is_volatile_type(std::meta::type_of(Mem)))
            return false;
        return std::is_arithmetic_v<F> || std::is_enum_v<F> ||
               std::is_same_v<F, std::string>;
    }()};

    /** Bind one field as a property through CLASS-ERASED accessors: the
        closures capture the member's byte offset as runtime state and take
        `nb::handle self`, so their closure TYPE — and therefore nanobind's
        `func_create` instantiation — depends only on the field type @a D.
        One instantiation per field type serves every welded class in the
        module, where `def_rw` instantiates per (class, field type): on a
        surface of ~4200 generated record classes that difference was the
        single largest code bucket in the binding shards.

        Installed through nanobind's type-erased `property_install`, NOT
        `class_<T>::def_prop_rw` — the latter is a member of `class_<T>` and
        would reintroduce the class dimension this path exists to remove.

        Why this is not UB, precisely:
        - `nb_inst_ptr(self)` is the SAME unadjusted instance pointer nanobind
          hands `def_rw`'s `const T&` parameter — `nb_type_get` performs no
          this-pointer adjustment anywhere (single-inheritance model, base and
          derived share an address), so the two paths see identical addresses
          in every case nanobind supports.
        - `static_cast<char*>(p) + offset` then casting to `D*` is the
          `offsetof` pattern: `std::meta::offset_of` yields the member's real
          ABI offset, so the final pointer's VALUE is the address of the live
          `D` subobject, and accessing a `D` through a `D*` that points at a
          `D` violates neither aliasing nor lifetime rules. The residual
          [expr.add] wording gap on char arithmetic over an object
          representation is the same one `offsetof` itself rests on (P1839);
          every implementation defines it.
        - Bit-fields (no addressable offset) and members inherited from bases
          (offset relative to the wrong class) never reach this path — see
          `_erasable_field`. */
    template <class D>
    static void _def_erased_field(nb::handle cls, const char* name,
                                  std::size_t offset, bool read_only,
                                  const char* doc) {
        auto get = [offset](nb::handle self) -> D {
            return *reinterpret_cast<const D*>(
                static_cast<const char*>(nb::inst_ptr<void>(self)) + offset);
        };
        nb::object get_p{doc ? nb::cpp_function(get, nb::is_method(),
                                                nb::is_getter(), doc)
                             : nb::cpp_function(get, nb::is_method(),
                                                nb::is_getter())};
        nb::object set_p{};
        if (!read_only) {
            auto set = [offset](nb::handle self, D value) {
                *reinterpret_cast<D*>(
                    static_cast<char*>(nb::inst_ptr<void>(self)) + offset) =
                    std::move(value);
            };
            set_p = nb::cpp_function(set, nb::is_method());
        }
        nb::detail::property_install(cls.ptr(), name, get_p.ptr(), set_p.ptr());
    }

    /** Bind data member @a Mem as an attribute.

        nanobind data members become Python properties (data descriptors on the
        class), so a `[[=welder::doc]]` on the member rides along as the property's
        `__doc__` — and thus reaches `.pyi` stubs. A const member — or one marked
        `[[=welder::mark::no_reassign]]` — is read-only (`def_ro`); an
        otherwise-mutable member is read/write (`def_rw`). The doc, when present, is
        passed as the property docstring. There is deliberately no setter docstring:
        a Python `property` surfaces only the getter's `__doc__`.

        A directly-declared member of immutable-converting type binds through
        the class-erased path (`_def_erased_field`) — observably identical,
        one template instantiation per field TYPE instead of per (class,
        field type). Everything else takes the member-pointer path below.
        @see welder::rod */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(auto& cls) {
        constexpr const char* name{
            ::welder::name_of<Mem, language, Style, ::welder::ent_kind::field>()};
        constexpr const char* doc{::welder::doc_of<Mem>()};
        // Read-only either because the member's type is const (def_rw's setter
        // would not compile) or because a `no_reassign` mark forces it on an
        // otherwise-mutable member (a whole-object rebind is rejected; in-place
        // mutation still writes through the reference_internal getter).
        constexpr bool read_only{std::meta::is_const_type(std::meta::type_of(Mem)) ||
                                 ::welder::member_no_reassign(Mem, language)};
        if constexpr (!std::meta::is_public(Mem)) {
            // A protected member (admitted under policy::weld_protected) binds
            // as a property over welder::detail::field_access — gcc-16 rejects
            // the dependent `&[:Mem:]` for protected data (see field_access).
            // Same semantics as def_rw: reference_internal getter.
            using fa = ::welder::detail::field_access<Mem>;
            if constexpr (read_only) {
                if constexpr (doc)
                    cls.def_prop_ro(name, &fa::get,
                                    nb::rv_policy::reference_internal, doc);
                else
                    cls.def_prop_ro(name, &fa::get,
                                    nb::rv_policy::reference_internal);
            } else {
                if constexpr (doc)
                    cls.def_prop_rw(name, &fa::get, &fa::set,
                                    nb::rv_policy::reference_internal, doc);
                else
                    cls.def_prop_rw(name, &fa::get, &fa::set,
                                    nb::rv_policy::reference_internal);
            }
        } else if constexpr (_erasable_field<
                                 Mem, typename std::remove_reference_t<
                                          decltype(cls)>::Type>) {
            using F = [:std::meta::remove_cv(std::meta::type_of(Mem)):];
            // .bytes is ptrdiff_t (P2996's member_offset); a real member's
            // offset is never negative, so the cast is value-preserving.
            _def_erased_field<F>(
                cls, name,
                static_cast<std::size_t>(std::meta::offset_of(Mem).bytes),
                read_only, doc);
        } else if constexpr (read_only) {
            if constexpr (doc)
                cls.def_ro(name, &[:Mem:], doc);
            else
                cls.def_ro(name, &[:Mem:]);
        } else {
            if constexpr (doc)
                cls.def_rw(name, &[:Mem:], doc);
            else
                cls.def_rw(name, &[:Mem:]);
        }
    }

    /** Bind the resolved property (@a Getter + optional @a Setter) as a Python
        property named @a name (driver-resolved).

        `def_prop_rw` / `def_prop_ro` over the spliced member pointers; the
        getter's `[[=welder::doc]]` becomes the property `__doc__` (only the
        getter's doc surfaces on a Python `property` — the `add_field`
        rationale). A `[[=welder::return_policy]]` on the getter is honored;
        unannotated, a reference/pointer-returning getter gets an explicit
        `reference_internal` (nanobind, unlike pybind11, does not default
        property getters to it — this matches `def_rw` and the pybind11 rod),
        while a by-value getter keeps nanobind's `automatic` (a move — an
        explicit reference policy there would dangle). @see welder::rod */
    template <class T, std::meta::info Getter, std::meta::info Setter>
    static void add_property(auto& cls, const char* name) {
        ::welder::validate_return_policy<Getter, language>();
        constexpr ::welder::rv_kind rvk{::welder::return_policy_of(Getter, language)};
        constexpr const char* doc{::welder::doc_of<Getter>()};
        auto def{[&](auto&&... extra) {
            if constexpr (Setter == std::meta::info{}) {
                cls.def_prop_ro(name, &[:Getter:],
                                std::forward<decltype(extra)>(extra)...);
            } else if constexpr (std::is_void_v<
                                     typename [:std::meta::return_type_of(Setter):]>) {
                cls.def_prop_rw(name, &[:Getter:], &[:Setter:],
                                std::forward<decltype(extra)>(extra)...);
            } else {
                // A value-returning setter (a fluent T& set_x(…)): discard the
                // return — the property protocol has no slot for it, and the
                // gate deliberately never checked it.
                using Arg = typename
                    [:std::meta::type_of(std::meta::parameters_of(Setter)[0]):];
                static constexpr auto sp{&[:Setter:]};
                cls.def_prop_rw(
                    name, &[:Getter:],
                    [](T& self, Arg v) { (self.*sp)(std::forward<Arg>(v)); },
                    std::forward<decltype(extra)>(extra)...);
            }
        }};
        constexpr nb::rv_policy rvp{[] {
            if constexpr (rvk != ::welder::rv_kind::automatic) {
                return _rv_policy(rvk);
            } else {
                constexpr auto rt{std::meta::return_type_of(Getter)};
                return std::meta::is_pointer_type(rt) ||
                               std::meta::is_lvalue_reference_type(rt)
                           ? nb::rv_policy::reference_internal
                           : nb::rv_policy::automatic;
            }
        }()};
        if constexpr (doc)
            def(rvp, doc);
        else
            def(rvp);
    }

    /** Bind method overload group @a Fns (name from `Fns[0]`; nanobind chains one
        `.def` per overload and dispatches at call time). @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_method(auto& cls) {
        constexpr const char* name{
            ::welder::name_of<Fns[0], language, Style, ::welder::ent_kind::method>()};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            _def_function<fn, Style>(name, [&cls](auto&&... a) {
                cls.def(std::forward<decltype(a)>(a)...);
            });
            // C++ default arguments: one truncated overload per omissible
            // arity (see _def_truncated for why the value cannot be restated).
            constexpr std::size_t d{::welder::detail::trailing_default_count<fn>()};
            if constexpr (d > 0)
                _def_default_truncations<
                    fn, typename std::remove_reference_t<decltype(cls)>::Type,
                    Style>(
                    name,
                    [&cls](auto&&... a) {
                        cls.def(std::forward<decltype(a)>(a)...);
                    },
                    std::make_index_sequence<d>{});
        }
    }

    /** Bind static-method overload group @a Fns. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_static_method(auto& cls) {
        constexpr const char* name{
            ::welder::name_of<Fns[0], language, Style,
                              ::welder::ent_kind::static_method>()};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            _def_function<fn, Style>(name, [&cls](auto&&... a) {
                cls.def_static(std::forward<decltype(a)>(a)...);
            });
            constexpr std::size_t d{::welder::detail::trailing_default_count<fn>()};
            if constexpr (d > 0)
                _def_default_truncations<fn, void, Style>(
                    name,
                    [&cls](auto&&... a) {
                        cls.def_static(std::forward<decltype(a)>(a)...);
                    },
                    std::make_index_sequence<d>{});
        }
    }

    /** Bind operator slot group @a Fns — one (operator, arity) slot whole,
        member and anchored *free* entries mixed. A member (or @a T-on-the-left
        free) entry binds under the slot's dunder; a free entry with @a T as the
        RIGHT operand binds under the REFLECTED dunder (`__rmul__`, or the
        mirrored comparison) through an operand-swapping wrapper. Binary
        arithmetic/comparison defs carry `nb::is_operator()`: a failed operand
        conversion returns `NotImplemented` (Python then tries the other
        operand's reflected method) instead of raising `TypeError`.
        @see welder::rod */
    template <class T, auto Fns>
    static void add_operator(auto& cls) {
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            if constexpr (::welder::detail::free_operator_reflected(fn, ^^T)) {
                _def_reflected_operator<T, fn>(cls);
            } else {
                _def_operator<fn, ::welder::rods::python::
                                      dunder_uses_not_implemented(fn)>(
                    ::welder::rods::python::operator_dunder(fn), cls,
                    std::make_index_sequence<
                        ::welder::detail::keep_alive_pairs<fn>().size()>{});
            }
        }
    }

    /** Synthesize the relational dunders from `operator<=>` group @a Fns via
        rewritten expressions (`a < b`, …), skipping the slots an explicit
        participating operator already @a Covered — the same semantics as the
        pybind11 rod (the shared walk is
        @ref welder::rods::python::synthesize_comparisons). @see welder::rod */
    template <class T, auto Fns, auto Covered>
    static void add_comparisons(auto& cls) {
        ::welder::rods::python::synthesize_comparisons<T, Fns, Covered>(
            [&cls](const char* name, auto fp) {
                cls.def(name, fp, nb::is_operator{});
            });
    }

    /** Bind the swept free ostream inserter @a Fn as `__str__` (via
        @ref welder::detail::stringify). @see welder::rod */
    template <class T, std::meta::info Fn>
    static void add_stringifier(auto& cls) {
        cls.def("__str__", &::welder::detail::stringify<T, Fn>);
    }

  private:
    /** Def operator @a Fn (member, or free with the anchor on the left) under
        dunder @a name. Unlike `_def_function`, never passes `nb::arg` names —
        Python's operator protocol is positional-only. Docstring,
        `return_policy` and `keep_alive`s ride along as usual; @a NotImpl
        appends `nb::is_operator()` (see `add_operator`).
        @tparam Fn the operator. @tparam K the keep_alive index pack. */
    template <std::meta::info Fn, bool NotImpl, class Cls, std::size_t... K>
    static void _def_operator(const char* name, Cls& cls,
                              std::index_sequence<K...>) {
        static constexpr auto ka{::welder::detail::keep_alive_pairs<Fn>()};
        ::welder::validate_return_policy<Fn, language>();
        constexpr nb::rv_policy rvp{
            _rv_policy(::welder::return_policy_of(Fn, language))};
        const std::string doc{::welder::function_docstring<Fn, DocStyle>()};
        auto def{[&](auto&&... extra) {
            if (doc.empty())
                cls.def(name, &[:Fn:], rvp,
                        nb::keep_alive<ka[K].nurse, ka[K].patient>()...,
                        std::forward<decltype(extra)>(extra)...);
            else
                cls.def(name, &[:Fn:], doc.c_str(), rvp,
                        nb::keep_alive<ka[K].nurse, ka[K].patient>()...,
                        std::forward<decltype(extra)>(extra)...);
        }};
        if constexpr (NotImpl)
            def(nb::is_operator{});
        else
            def();
    }

    /** Bind reflected free operator @a Fn (@a T is its right operand) under its
        reflected dunder, swapping the operands back into declaration order for
        the C++ call. @tparam T the anchor type. @tparam Fn the operator. */
    template <class T, std::meta::info Fn>
    static void _def_reflected_operator(auto& cls) {
        constexpr const char* name{::welder::rods::python::reflected_dunder(Fn)};
        if constexpr (name != nullptr) {
            ::welder::validate_return_policy<Fn, language>();
            using Lhs = typename
                [:std::meta::type_of(std::meta::parameters_of(Fn)[0]):];
            cls.def(
                name,
                [](const T& self, Lhs lhs) {
                    return [:Fn:](static_cast<Lhs&&>(lhs), self);
                },
                nb::is_operator{});
        }
    }

  public:

    // --- enum binding -------------------------------------------------------

    /** Create the `nb::enum_<E>` handle (a non-null @a doc becomes its docstring).

        `nb::is_arithmetic()` makes it a Python `enum.IntEnum`, so enumerators are
        int-convertible (`int(E.Value)`) and compare against ints — matching the
        pybind11 backend, whose `py::native_enum` also binds an `enum.IntEnum`.
        @see welder::rod */
    template <class E>
    static auto make_enum(module_type& m, const char* name,
                          const ::welder::detail::enum_doc& ed) {
        // ed's summary + documented enumerators fold into the class docstring under
        // DocStyle (an Attributes section) — the one place nanobind's stub generator
        // carries an enumerator's doc into the .pyi. Empty (wholly undocumented) is
        // branched out rather than passed. nb::enum_ builds the type in its ctor and
        // copies the doc, so the transient string here is safe.
        const std::string doc{DocStyle::format_enum(ed)};
        if (!doc.empty())
            return nb::enum_<E>(m, name, doc.c_str(), nb::is_arithmetic());
        return nb::enum_<E>(m, name, nb::is_arithmetic());
    }

    /** Create the `nb::enum_<E>` for a **nested** member enum, scoped to its
        enclosing type's class handle — Python sees `module.Outer.Mode`, and an
        *unscoped* nested enum's `export_values()` lands its enumerators on the
        class (mirroring C++'s `Outer::red`). @a ed folds in as for @ref make_enum.
        @see welder::rod */
    template <class E>
    static auto make_nested_enum(module_type&, auto& outer_cls, const char* name,
                                 const ::welder::detail::enum_doc& ed) {
        const std::string doc{DocStyle::format_enum(ed)};
        if (!doc.empty())
            return nb::enum_<E>(outer_cls, name, doc.c_str(), nb::is_arithmetic());
        return nb::enum_<E>(outer_cls, name, nb::is_arithmetic());
    }

    /** Add enumerator @a Enum to the enum handle. @see welder::rod */
    template <std::meta::info Enum, class Style = ::welder::naming::none>
    static void add_enumerator(auto& e) {
        e.value(
            ::welder::name_of<Enum, language, Style, ::welder::ent_kind::enumerator>(),
            [:Enum:]);
    }

    /** Finalize enum @a E: export an unscoped enum's values into the enclosing scope.
        @see welder::rod */
    template <class E>
    static void finish_enum(auto& e) {
        // Mirror C++ scope semantics: an unscoped enum's enumerators are visible
        // unqualified, so export them into the enclosing scope; a scoped enum's
        // are reached as E.Value, so leave them scoped.
        if constexpr (!std::is_scoped_enum_v<E>)
            e.export_values();
    }

    // --- namespace / module binding -----------------------------------------

    /** Open a per-module session: a dict accumulating live (mutable-variable)
        properties; _install_live_properties() applies them in one `__class__` swap
        at close. @see welder::rod */
    static nb::dict open_module(module_type&) { return nb::dict{}; }

    /** Set the (sub)module docstring.

        nanobind's `module_` exposes no `doc()` setter, so the docstring is written
        straight to the module's `__doc__` attribute. @see welder::rod */
    static void set_module_doc(module_type& m, const char* doc) {
        m.attr("__doc__") = doc;
    }

    /** Bind free-function overload group @a Fns as one module-level function
        (name from `Fns[0]`; one chained `.def` per overload).

        A non-null @a name overrides the resolved name (including any `weld_as`),
        used verbatim; `nullptr` falls back to the styled/`weld_as` name.
        @return the bound function object (`m.attr(name)`) — the handle for
                further hand-registration. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static nb::object add_function(module_type& m, const char* name = nullptr) {
        const char* fn_name{::welder::name_of_or<Fns[0], language, Style,
                                                 ::welder::ent_kind::function>(name)};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            _def_function<fn, Style>(fn_name, [&m](auto&&... a) {
                m.def(std::forward<decltype(a)>(a)...);
            });
            constexpr std::size_t d{::welder::detail::trailing_default_count<fn>()};
            if constexpr (d > 0)
                _def_default_truncations<fn, void, Style>(
                    fn_name,
                    [&m](auto&&... a) { m.def(std::forward<decltype(a)>(a)...); },
                    std::make_index_sequence<d>{});
        }
        return m.attr(fn_name);
    }

    /** Bind namespace variable @a Var as a module attribute.

        A const/constexpr variable becomes a value snapshot; a mutable one becomes a
        live get/set property over the C++ global (accumulated in @a live). A non-null
        @a name_override is used verbatim (beating any `weld_as`); `nullptr` falls back
        to the styled/`weld_as` name. @see welder::rod */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, nb::dict& live,
                             const char* name_override = nullptr) {
        const char* name{
            ::welder::name_of_or<Var, language, Style, ::welder::ent_kind::variable>(
                name_override)};
        if constexpr (std::meta::is_const_type(std::meta::type_of(Var))) {
            m.attr(name) = [:Var:]; // immutable: a value snapshot at bind time
        } else {
            // Mutable: a live property over the C++ global. The descriptors take a
            // leading `self` (the module) and ignore it.
            auto property{nb::module_::import_("builtins").attr("property")};
            live[name] = property(
                nb::cpp_function([](nb::object) { return [:Var:]; }),
                nb::cpp_function([](nb::object,
                                    typename [:std::meta::type_of(Var):] v) {
                    [:Var:] = v;
                }));
        }
    }

    /** Create a submodule named @a name under @a m. @see welder::rod */
    static module_type add_submodule(module_type& m, const char* name) {
        return m.def_submodule(name);
    }

    /** Bind STL @a Container **opaquely** — by reference, with live mutation — under
        @a name, the driver's route for a welded container alias (see
        `<welder/containers.hpp>`).

        A sequence (`std::vector`/`std::deque`) becomes an `nb::bind_vector` class:
        `append` (=`push_back`), `__getitem__`/`__setitem__`, slicing, `__len__`,
        `__iter__` — mutation writes through to the C++ object (a `def_rw` member of
        it hands out a live reference). For a welded-class element, `__getitem__` /
        `__iter__` themselves hand out a **live reference** aliasing the C++ element
        (`rv_policy::reference_internal`, kept alive to the container), so
        `v[i].field = x` writes through; a scalar element is returned by value (a
        copy). nanobind has no buffer protocol, so for a
        **scalar** element type (arithmetic, not `bool`) the class gains an
        `__array__` returning an `nb::ndarray` **zero-copy** view of `data()` (kept
        alive to the container), so `numpy.asarray(v)` sees the live buffer. A map
        (`std::map`/`std::unordered_map`) becomes an `nb::bind_map` class whose
        `__getitem__` likewise hands out a live reference to the mapped value.

        The container must be declared opaque (`WELDER_OPAQUE(Container)`) at
        namespace scope — nanobind otherwise hard-errors when the stl caster for it is
        also visible (or silently copy-converts it). @see welder::rod */
    template <class Container, class Style = ::welder::naming::none>
    static void bind_container(module_type& m, const char* name) {
        constexpr ::welder::container_kind kind{
            ::welder::container_kind_of(^^Container)};
        if constexpr (kind == ::welder::container_kind::sequence) {
            // reference_internal (not nanobind's automatic_reference default,
            // which downgrades an lvalue-reference return to a *copy*) so
            // __getitem__/__iter__ hand out a live reference aliasing the C++
            // element — v[i].field = x writes through, matching pybind11's own
            // bind_vector default. A scalar element ignores the policy and casts
            // by value (a copy, as intended).
            auto cls{
                nb::bind_vector<Container, nb::rv_policy::reference_internal>(
                    m, name)};
            using Elem = typename Container::value_type;
            using Size = typename Container::size_type;
            // `reserve(n)`: pre-grow capacity so a following run of append()/new()
            // does not reallocate — the efficient way to bulk-populate, and the way
            // to keep element references valid across that run (no reallocation ⇒ no
            // move). Only where the container actually has reserve (std::vector;
            // std::deque has none).
            if constexpr (requires(Container& c, Size n) { c.reserve(n); })
                cls.def(
                    "reserve", [](Container& v, Size n) { v.reserve(n); },
                    nb::arg("n"),
                    "Pre-allocate capacity for at least n elements (no-op if capacity "
                    "already exceeds n). Prevents reallocation — and reference "
                    "invalidation — across a following run of append()/new().");
            // `resize(n)`: grow/shrink to exactly n elements, value-initializing any
            // new tail elements — allocate-then-fill-by-index bulk population. Needs a
            // default-constructible element (the requires-expression gates it).
            if constexpr (requires(Container& c, Size n) { c.resize(n); })
                cls.def(
                    "resize", [](Container& v, Size n) { v.resize(n); },
                    nb::arg("n"),
                    "Resize to exactly n elements, value-initializing any new tail "
                    "elements. Shrinking or reallocating invalidates references.");
            // `new()`: default-construct an element in place at the back and hand
            // back a **live reference** to it (reference_internal, kept alive to the
            // container) — so generic Python code can grow a container of welded
            // structs without importing the element type just to construct one
            // (`e = v.new(); e.field = x`). Class elements only: a scalar would be
            // returned by value (a dead copy, mutations lost), which is a footgun, so
            // it's omitted there — use `append(value)`. Standard bind_vector caveat:
            // a later append/clear may reallocate and invalidate the reference.
            if constexpr (std::is_class_v<Elem> &&
                          std::is_default_constructible_v<Elem>) {
                cls.def(
                    "new",
                    [](Container& v) -> Elem& { return v.emplace_back(); },
                    nb::rv_policy::reference_internal,
                    "Default-construct a new element in place at the end and return "
                    "a live reference to it.");
            }
            _numpy_view<Container>(cls);
        } else if constexpr (kind == ::welder::container_kind::fixed_sequence) {
            // std::array has no nb::bind_array; hand-write the vector protocol
            // minus the size-changing ops.
            _bind_array<Container>(m, name);
        } else {
            // reference_internal for __getitem__ (see the sequence branch): the
            // mapped value is handed out as a live reference, so m[k].field = x
            // writes through — a copy for a scalar value type.
            nb::bind_map<Container, nb::rv_policy::reference_internal>(m, name);
        }
    }

    /** Bind fixed-size sequence @a Container (`std::array<T, N>`) **opaquely** — by
        reference, with element write-through — under @a name.

        Neither framework ships a `bind_array`, so this hand-writes the `bind_vector`
        surface **minus the size-changing ops**: `__len__` (the constant `N`),
        `__getitem__` / `__setitem__` (a welded-class element handed out as a live
        `reference_internal` alias so `a[i].field = x` writes through; a scalar
        returned by value), `__iter__`, and the same zero-copy NumPy view as a scalar/
        POD vector (@ref _numpy_view). There is deliberately **no** `append`/`insert`/
        `pop`/`extend`/`clear` (a fixed array cannot resize). Whole-attribute
        assignment from a length-`N` sequence still works: a `__init__` from any
        iterable (length checked — a wrong length raises `ValueError`) is registered as
        an implicit conversion, so `obj.arr = [...]` rebinds through `def_rw`. A
        length-changing slice assignment is rejected — only integer indices are bound,
        so a `slice` subscript raises `TypeError`. @see welder::rod */
    template <class Container>
    static void _bind_array(module_type& m, const char* name) {
        using Elem = typename Container::value_type;
        constexpr std::size_t N{std::tuple_size_v<Container>};
        constexpr nb::rv_policy P{nb::rv_policy::reference_internal};
        auto cls{nb::class_<Container>(m, name)};
        cls.def(nb::init<>(), "Default constructor")
            .def(nb::init<const Container&>(), "Copy constructor")
            .def("__len__", [](const Container&) { return N; })
            .def("__bool__", [](const Container&) { return N != 0; })
            .def(
                "__iter__",
                [](Container& a) {
                    return nb::make_iterator<P>(nb::type<Container>(), "Iterator",
                                                a.begin(), a.end());
                },
                nb::keep_alive<0, 1>())
            .def(
                "__getitem__",
                [](Container& a, Py_ssize_t i) -> Elem& {
                    return a[_wrap_index(i, N)];
                },
                P)
            .def("__setitem__", [](Container& a, Py_ssize_t i, const Elem& v) {
                a[_wrap_index(i, N)] = v;
            });
        // Whole-attribute assignment: construct from a length-N iterable, registered
        // as an implicit conversion so `def_rw`'s setter accepts a plain list/tuple.
        cls.def(
            "__init__",
            [](Container* self, nb::typed<nb::iterable, Elem> seq) {
                // Mirror bind_vector's exception safety: destruct the in-place object
                // if the fill fails (a wrong length, an element that won't cast), so a
                // welded-class element leaves no default-constructed leftovers behind.
                Container* a{new (self) Container{}};
                try {
                    std::size_t i{0};
                    for (nb::handle h : seq) {
                        if (i >= N)
                            throw nb::value_error(
                                "expected a sequence of the array's fixed length");
                        (*a)[i++] = nb::cast<Elem>(h);
                    }
                    if (i != N)
                        throw nb::value_error(
                            "expected a sequence of the array's fixed length");
                } catch (...) {
                    a->~Container();
                    throw;
                }
            },
            "Construct from a length-N iterable");
        nb::implicitly_convertible<nb::iterable, Container>();
        _numpy_view<Container>(cls);
    }

    /** Give contiguous sequence class @a cls the zero-copy NumPy view its element type
        supports: an `__array__` returning a live `nb::ndarray` for an arithmetic
        (non-`bool`) element, or the structured `__array_interface__` dict for a POD
        struct element (@ref _array_interface). A non-contiguous / non-viewable element
        gets neither. Shared by the `std::vector` and `std::array` paths (both expose
        `.data()`/`.size()`). @tparam Container the contiguous sequence. */
    template <class Container, class Cls>
    static void _numpy_view(Cls& cls) {
        using Elem = typename Container::value_type;
        if constexpr (::welder::container_is_contiguous(^^Container) &&
                      std::is_arithmetic_v<Elem> && !std::is_same_v<Elem, bool>) {
            // numpy's __array__ protocol: numpy 2.x calls it with dtype/copy
            // (positional or keyword); accept and ignore both and always hand
            // back a live view — copy=True is honored by numpy copying the view.
            cls.def(
                "__array__",
                [](Container& v, nb::handle, nb::handle) {
                    return nb::ndarray<nb::numpy, Elem, nb::ndim<1>, nb::c_contig>(
                        v.data(), {v.size()}, nb::find(&v));
                },
                nb::arg("dtype") = nb::none(), nb::arg("copy") = nb::none());
        } else if constexpr (::welder::container_is_contiguous(^^Container) &&
                             ::welder::rods::python::pod_array_eligible<Elem>()) {
            // Contiguous POD-struct element: expose the numpy array-interface
            // protocol (structured, zero-copy, numpy-free view of data()).
            _array_interface<Container, Elem>(cls);
        }
    }

    /** Normalize a Python index @a i (allowing one level of negative wrap-around)
        against length @a n, raising `IndexError` when out of range — the fixed-array
        `__getitem__`/`__setitem__` bounds check (the `bind_vector` `wrap` analogue,
        reproduced here so the array path does not lean on a framework-internal
        symbol). */
    static std::size_t _wrap_index(Py_ssize_t i, std::size_t n) {
        if (i < 0)
            i += static_cast<Py_ssize_t>(n);
        if (i < 0 || static_cast<std::size_t>(i) >= n)
            throw nb::index_error("array index out of range");
        return static_cast<std::size_t>(i);
    }

    /** Give the opaque `std::vector<Elem>` class @a cls a `__array_interface__`
        property — the numpy array-interface dict (`numpy.asarray(v)` reads it),
        a **structured, zero-copy, writable** view of `data()`, numpy-free (a plain
        Python attribute); the field `descr` is reflected from @a Elem's POD layout
        (@ref welder::rods::python::ai_descr). */
    template <class Container, class Elem, class Cls>
    static void _array_interface(Cls& cls) {
        namespace pyai = ::welder::rods::python;
        cls.def_prop_ro("__array_interface__", [](Container& v) {
            static constexpr auto d{pyai::ai_descr<^^Elem>()};
            nb::list descr{};
            for (auto [nm, ts] : d)
                descr.append(nb::make_tuple(nm, ts));
            nb::dict out{};
            out["shape"] = nb::make_tuple(v.size());
            out["typestr"] = pyai::ai_typestr<Elem>();
            out["descr"] = descr;
            out["data"] = nb::make_tuple(
                reinterpret_cast<std::uintptr_t>(v.data()), false);
            out["version"] = 3;
            return out;
        });
    }

    /** Close the session: apply any accumulated live properties. @see welder::rod */
    static void close_module(module_type& m, nb::dict& live) {
        if (live.size() != 0)
            _install_live_properties(m, live);
    }
};

static_assert(::welder::rod<rod<>>,
              "welder::rods::nanobind::rod<> must satisfy welder::rod");

} // namespace welder::rods::nanobind
