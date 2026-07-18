#pragma once
/** @file
    welder pybind11 rod (header-only).

    This is a *thin* rod: it implements welder's rod contract
    (`<welder/welder.hpp>`) for pybind11 and hands the traversal/resolution off to
    welder's generic driver. All the language-agnostic work — deciding which
    members bind, gating bindability, folding docstrings, walking bases and
    namespaces — lives in the core; only the pybind11 emission primitives are here.
    The nanobind / lua backends mirror this file against their own frameworks.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`).
    This header exposes exactly one thing: the rod template
    `welder::rods::pybind11::rod<DocStyle = google_style>`, to plug into
    `welder::welder` (`rod<>` for the default Google docstring style;
    `rod<numpy_style>` / `rod<sphinx_style>` for the other dialects):
    @code
    #include <pybind11/pybind11.h>
    #include <welder/rods/python/pybind11/rod.hpp>
    PYBIND11_MODULE(mymod, m) {
        welder::welder<welder::rods::pybind11::rod<>>::weld_type<MyType>(m);
    }
    @endcode
    (For the `WELDER_MODULE` entry-point macro, include this directory's
    `module.hpp` instead.)
*/
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
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

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h> // py::native_enum (stdlib enum binding)

namespace welder::inline v0::rods::pybind11 {

// Inside `welder::rods::pybind11`, the unqualified name `pybind11` resolves to *this*
// namespace, not the library. Alias the real one once — the way pybind11's own
// docs spell it — and use `py::` for every library reference below.
namespace py = ::pybind11;

/** The pybind11 rod: a stateless policy type satisfying @ref welder::rod.

    Its public static members are the pybind11 emission primitives welder's driver
    calls; the driver supplies all the reflection-derived decisions. Each implements
    the correspondingly-named hook of the @ref welder::rod contract (and
    @ref welder::caster_oracle) — every one carries a `@see` back to it, where the
    shared parameter and return-value semantics are documented once rather than
    repeated on each backend's mirror. The `protected` members below are
    pybind11-specific implementation helpers (prefixed `_`), not part of the contract.

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
    using module_type = py::module_;          /**< pybind11's module handle. */

  protected:
    // --- implementation helpers (not part of the welder::rod contract) --

    /** Whether pybind11 can only convert @a T via *runtime class registration*.

        True iff @a T's caster is (or derives from) the generic `type_caster_base`
        fallback, which looks @a T up in pybind11's registered-types map. True for
        program-defined classes and enums; false for scalars, strings and the
        pybind11 wrapper types (`py::object`, `py::dict`, …). This is the one
        bindability fact welder's core cannot know on its own; it drives
        `has_native_caster` below.

        Two things it deliberately does NOT see through — both making it
        *conservative* (it may over-report needs-registration, never under-report):
        - It reads @a T's *caster type* at compile time, so it reports whether @a T
          *needs* a `class_`/`native_enum` — never whether one will actually exist at
          runtime. A class the user hand-registers with `py::class_` (or a
          third-party library registers) but does not weld still reads `true`, so
          welder requires `welded_for` and rejects it: a false positive, resolved by
          the deferred `trust_bindable` escape hatch, since the out-of-band
          registration is invisible here.
        - "native" is relative to the TU's includes: `std::complex` /
          `std::function` / `std::chrono` / `std::filesystem::path` are native only
          when their converter header (`<pybind11/complex.h>`, `functional.h`,
          `chrono.h`, `stl/filesystem.h`) is included; otherwise they fall to the
          class-registration fallback. That is correct — without the header pybind11
          genuinely cannot convert them.

        A user `type_caster` is trusted (reads `false`) only when it is
        *self-contained* — it does not itself derive from `type_caster_base` (e.g. a
        `PYBIND11_TYPE_CASTER` caster). One that derives from `type_caster_base`
        still needs @a T registered, so it correctly still reads `true`.

        **Enums are forced into the needs-registration bucket**: pybind11 3's
        dedicated enum caster does not derive `type_caster_base`, but it converts
        only once the enum is registered (`py::native_enum` / legacy `py::enum_`)
        — an unregistered enum raises at call time and renders raw C++ names in
        docstrings/stubs. Forcing it keeps welder's gate honest (a welded enum's
        registration is required) and matches every other rod.

        @tparam T the type whose caster to classify.
    */
    template <class T>
    static constexpr bool _needs_registration =
        std::is_enum_v<std::remove_cvref_t<T>> ||
        std::is_base_of_v<py::detail::type_caster_base<py::detail::intrinsic_t<T>>,
                          py::detail::make_caster<T>>;

    /** Map welder's neutral @ref welder::rv_kind to pybind11's
        `return_value_policy`.

        pybind11 has no analogue of nanobind's `none`, so `rv_kind::none` is
        rejected at the bind site (a `static_assert` in @ref _def_function) rather
        than mapped here; it falls through to `automatic` only to keep this total.
        @param k the neutral policy.
        @return the pybind11 policy. */
    static consteval py::return_value_policy _return_value_policy(::welder::rv_kind k) {
        switch (k) {
        case ::welder::rv_kind::automatic:           return py::return_value_policy::automatic;
        case ::welder::rv_kind::automatic_reference: return py::return_value_policy::automatic_reference;
        case ::welder::rv_kind::take_ownership:      return py::return_value_policy::take_ownership;
        case ::welder::rv_kind::copy:                return py::return_value_policy::copy;
        case ::welder::rv_kind::move:                return py::return_value_policy::move;
        case ::welder::rv_kind::reference:           return py::return_value_policy::reference;
        case ::welder::rv_kind::reference_internal:  return py::return_value_policy::reference_internal;
        case ::welder::rv_kind::none:                break; // no pybind11 equivalent
        }
        return py::return_value_policy::automatic;
    }

    /** Register the function/method reflected by @a Fn onto a pybind11 target.

        The folded docstring is passed when non-empty, and `py::arg(name)...` when
        every parameter is named (so Python callers see real keyword arguments, not
        `arg0`/`arg1`). Two call policies ride along as trailing `.def` extras: the
        `[[=welder::return_policy]]` (mapped to `return_value_policy`, always passed
        — `automatic` is pybind11's default, so an unannotated call is unchanged)
        and each `[[=welder::keep_alive]]` (spliced as `py::keep_alive<nurse,
        patient>()`). A reference-category policy on a by-value return is rejected
        first (@ref welder::validate_return_policy).
        @tparam Fn a reflection of the function.
        @tparam Def the target-adapter callable type.
        @tparam I the parameter index pack.
        @tparam K the keep_alive-dependency index pack.
        @param name     the Python name.
        @param def_into adapts the target — `cls.def`, `cls.def_static`, or `m.def`.
    */
    template <std::meta::info Fn, class Def, std::size_t... I, std::size_t... K>
    static void _def_function(const char* name, Def def_into,
                              std::index_sequence<I...>, std::index_sequence<K...>) {
        static constexpr auto names{::welder::detail::param_names<Fn>()};
        static constexpr auto ka{::welder::detail::keep_alive_pairs<Fn>()};
        ::welder::validate_return_policy<Fn, language>();
        constexpr ::welder::rv_kind rvk{::welder::return_policy_of(Fn, language)};
        static_assert(rvk != ::welder::rv_kind::none,
                      "welder: return_policy 'none' has no pybind11 equivalent "
                      "(it is nanobind-only) — choose another policy");
        constexpr py::return_value_policy rvp{_return_value_policy(rvk)};
        const std::string doc{::welder::function_docstring<Fn, DocStyle>()};
        if constexpr (::welder::detail::all_params_named<Fn>()) {
            if (doc.empty())
                def_into(name, &[:Fn:], py::arg(names[I])..., rvp,
                         py::keep_alive<ka[K].nurse, ka[K].patient>()...);
            else
                def_into(name, &[:Fn:], doc.c_str(), py::arg(names[I])..., rvp,
                         py::keep_alive<ka[K].nurse, ka[K].patient>()...);
        } else {
            if (doc.empty())
                def_into(name, &[:Fn:], rvp,
                         py::keep_alive<ka[K].nurse, ka[K].patient>()...);
            else
                def_into(name, &[:Fn:], doc.c_str(), rvp,
                         py::keep_alive<ka[K].nurse, ka[K].patient>()...);
        }
    }

    /** Convenience overload: derive the parameter and keep_alive index sequences
        from @a Fn. */
    template <std::meta::info Fn, class Def>
    static void _def_function(const char* name, Def def_into) {
        _def_function<Fn>(
            name, def_into,
            std::make_index_sequence<std::meta::parameters_of(Fn).size()>{},
            std::make_index_sequence<
                ::welder::detail::keep_alive_pairs<Fn>().size()>{});
    }

    /** Register `py::init<P0, P1, …>()` for constructor @a Ctor.

        Names the parameters (`py::arg`) when all are named, otherwise positional.
        @tparam Ctor a reflection of the constructor.
        @tparam I the parameter index pack.
        @param cls the class handle.
    */
    template <std::meta::info Ctor, std::size_t... I>
    static void _def_init(auto& cls, std::index_sequence<I...>) {
        static constexpr auto params{::welder::detail::param_types<Ctor>()};
        static constexpr auto names{::welder::detail::param_names<Ctor>()};
        if constexpr (::welder::detail::all_params_named<Ctor>())
            cls.def(py::init<typename [:params[I]:]...>(), py::arg(names[I])...);
        else
            cls.def(py::init<typename [:params[I]:]...>());
    }

    /** The subclass-faithful engine behind `__copy__`/`__deepcopy__`.

        Mirrors what Python's own copy machinery does for a pure-Python object —
        state transfer, never `__init__`: an uninitialized shell of the
        instance's *dynamic* type (`type(self).__new__(type(self))`, so a Python
        subclass copies as itself), the C++ payload copy-constructed in place by
        re-running the registered copy `__init__` on the shell (for a subclass
        shell pybind11 constructs the *alias* (trampoline) payload — which is why
        the trampoline needs a copy-from-base constructor — so the copy keeps
        dispatching virtuals into Python), then the instance `__dict__` carried
        over. With @a memo (the `__deepcopy__` path) the fresh object is
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
    static py::object _copy_instance(py::handle self, py::object* memo) {
        py::object cls{py::type::of(self)};
        py::object out{cls.attr("__new__")(cls)};
        if (memo)
            (*memo)[py::int_(reinterpret_cast<std::uintptr_t>(self.ptr()))] =
                out;
        py::type::of<T>().attr("__init__")(out, self);
        if (py::hasattr(self, "__dict__")) {
            py::object d{self.attr("__dict__")};
            if (memo)
                d = py::module_::import("copy").attr("deepcopy")(d, *memo);
            out.attr("__dict__").attr("update")(d);
        }
        for (py::handle name :
             py::module_::import("copyreg").attr("_slotnames")(cls)) {
            if (!py::hasattr(self, name))
                continue; // an unassigned slot stays unassigned on the copy
            py::object v{py::getattr(self, name)};
            if (memo)
                v = py::module_::import("copy").attr("deepcopy")(v, *memo);
            py::setattr(out, name, v);
        }
        return out;
    }

    /** Synthesize a field constructor for a baseless aggregate @a T.

        Emits `py::init([](F0 f0, …) { return T{f0, …}; }, py::arg("f0"), …)` so
        Python can build it from field values (`T(f0, f1)`) rather than only
        default-construct then assign.
        @tparam T the aggregate type.
        @tparam I the field index pack.
        @param cls the class handle.
    */
    template <class T, std::size_t... I>
    static void _def_aggregate_init(auto& cls, std::index_sequence<I...>) {
        static constexpr auto fields{::welder::detail::aggregate_fields<T>()};
        cls.def(py::init([](typename [:std::meta::type_of(fields[I]):]... args) {
                    return T{std::move(args)...};
                }),
                py::arg(std::define_static_string(
                    std::meta::identifier_of(fields[I])))...);
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
    static void _install_live_properties(py::module_& m, py::dict props) {
        auto builtins{py::module_::import("builtins")};
        auto subclass{builtins.attr("type")(
            py::str("welder_live_module"),
            py::make_tuple(m.attr("__class__")), props)};
        // The dynamically-created class would otherwise carry
        // `__module__ == "_frozen_importlib"` (the frame that ran module init).
        // pybind11 copies a module *scope*'s `__module__` onto every function later
        // defined on it, so any function welded onto `m` after this swap — e.g. a
        // `weld_function` following a mutable `weld_variable` on the same handle —
        // would be misattributed in generated stubs. Stamp the module's own name so
        // those functions keep the right `__module__`.
        subclass.attr("__module__") = m.attr("__name__");
        m.attr("__class__") = subclass;
    }

    /** Construct `py::class_<T, NativeBases...>` from a reflected base-type array.

        A non-null @a doc becomes the class docstring (pybind11 treats a bare
        `const char*` extra as the doc); `nullptr` is branched out rather than
        passed, since pybind11 would `strdup` it.
        @tparam T     the class type.
        @tparam Bases the static array of native base type reflections.
        @tparam I     the base index pack.
        @param scope the registration scope — the module, or (for a nested type)
                     the enclosing class handle; pybind11 accepts any handle.
        @param name  the Python class name.
        @param doc   the class docstring, or `nullptr`.
    */
    template <class T, class Trampoline, auto Bases, std::size_t... I>
    static auto _make_class(py::handle scope, const char* name, const char* doc,
                            std::index_sequence<I...>) {
        if constexpr (std::is_void_v<Trampoline>) {
            if (doc)
                return py::class_<T, typename [:Bases[I]:]...>(scope, name, doc);
            return py::class_<T, typename [:Bases[I]:]...>(scope, name);
        } else {
            // The trampoline is an extra `py::class_` template argument; Python
            // subclasses instantiate it, so their overrides capture C++ virtual calls.
            if (doc)
                return py::class_<T, Trampoline, typename [:Bases[I]:]...>(scope, name, doc);
            return py::class_<T, Trampoline, typename [:Bases[I]:]...>(scope, name);
        }
    }

    /** The trampoline-aware class factory over an arbitrary registration
        @a scope — the shared body of `make_class` (scope = the module) and
        `make_nested_class` (scope = the enclosing class handle).

        A type carrying virtual methods is bound *overridable* — it must register
        a trampoline (so Python subclasses can override those virtuals) or opt
        out with `[[=welder::rods::python::bind_flat]]`. When a trampoline is
        present, its coverage of @a T's virtuals is checked at compile time.
        @see welder::rods::python::trampoline_for */
    template <class T, auto Bases, std::size_t... I>
    static auto _make_class_at(py::handle scope, const char* name,
                               const char* doc, std::index_sequence<I...> seq) {
        namespace py_ = ::welder::rods::python;
        if constexpr (py_::has_virtual_methods(^^T)) {
            // Resolve the trampoline: an explicit `trampoline_for<T>` wins; otherwise
            // scan T's namespace for a `[[=trampoline]]`-annotated subclass.
            constexpr auto scanned{py_::scanned_trampoline_of(^^T)};
            static_assert(
                py_::trampoline_for<T> != std::meta::info{} || !scanned.ambiguous,
                "welder: more than one [[=welder::rods::python::trampoline]] class in "
                "this namespace derives from T; disambiguate by specializing "
                "welder::rods::python::trampoline_for<T>.");
            constexpr std::meta::info tramp{py_::trampoline_for<T> != std::meta::info{}
                                                ? py_::trampoline_for<T>
                                                : scanned.type};
            if constexpr (tramp != std::meta::info{}) {
                using Trampoline = [:tramp:];
                static_assert(
                    py_::trampoline_covers(^^T, ^^Trampoline),
                    "welder: the trampoline registered for this type does not "
                    "override all of its virtual methods; every virtual needs an "
                    "override forwarding to Python (see WELDER_PY_OVERRIDE).");
                return _make_class<T, Trampoline, Bases>(scope, name, doc, seq);
            } else {
                static_assert(
                    py_::bound_flat(^^T),
                    "welder: this welded type has virtual methods but no trampoline "
                    "is registered, so a Python subclass could not override them. "
                    "Register one — a [[=welder::rods::python::trampoline]] subclass "
                    "in T's namespace, or a welder::rods::python::trampoline_for<T> "
                    "specialization — or annotate T with "
                    "[[=welder::rods::python::bind_flat]] to bind it non-overridably.");
                return _make_class<T, void, Bases>(scope, name, doc, seq);
            }
        } else {
            return _make_class<T, void, Bases>(scope, name, doc, seq);
        }
    }

  public:
    // --- caster oracle + emission primitives (the welder::rod contract) --

    /** `caster_oracle`: @a T is convertible without welder registering a class for
        it iff pybind11 does *not* fall back to runtime class registration.
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

    /** Create the `py::class_<T, Bases…>` handle, weaving in a trampoline when @a T
        is a welded virtual type with a registered
        `welder::rods::python::trampoline_for`.

        A type carrying virtual methods is bound *overridable* — it must register a
        trampoline (so Python subclasses can override those virtuals) or opt out with
        `[[=welder::rods::python::bind_flat]]`. When a trampoline is present, its
        coverage of @a T's virtuals is checked at compile time. @see _make_class
        @see welder::rods::python::trampoline_for @see welder::rod */
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* doc,
                           std::index_sequence<I...> seq) {
        return _make_class_at<T, Bases>(m, name, doc, seq);
    }

    /** Create the `py::class_` for a **nested** member type @a T, registered
        under its enclosing type's class handle rather than the module — Python
        then sees it as `module.Outer.Inner` (and `__qualname__` nests), exactly
        like a hand-written `py::class_<Outer::Inner>(outer_cls, "Inner")`. Same
        trampoline weaving as `make_class`. @see welder::rod */
    template <class T, auto Bases, std::size_t... I>
    static auto make_nested_class(module_type&, auto& outer_cls, const char* name,
                                  const char* doc, std::index_sequence<I...> seq) {
        return _make_class_at<T, Bases>(outer_cls, name, doc, seq);
    }

    /** Bind @a T's whole constructor set (a chained-def framework just loops it):
        the default constructor when @a HasDefault, a `py::init<…>` per member of
        @a Ctors, and the synthesized aggregate field constructor when
        @a Aggregate.

        @a Copyable (the carriage-admitted copy constructor) becomes three
        things: a Python-visible copy constructor (`T(other)`, the
        `py::init<const T&>` that also serves as the in-place construction
        vehicle) and the copy protocol — `__copy__` and `__deepcopy__(memo)`,
        both **subclass-faithful** via @ref _copy_instance: a Python subclass
        instance copies as its own type, `__dict__` and virtual dispatch intact,
        with the C++ payload duplicated by the copy constructor (whose
        deep/shallow distinction is its own: value members duplicate, a pointer
        member copies as a pointer). The memo parameter is typed `object`, not
        `dict` — a bare `dict` in the generated stub fails strict mypy
        (disallow_any_generics). @see _def_init @see _def_aggregate_init
        @see welder::rod */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate, bool Copyable>
    static void add_constructors(auto& cls) {
        if constexpr (HasDefault)
            cls.def(py::init<>());
        if constexpr (Copyable) {
            // A trampolined type must keep the copy faithful for Python
            // subclasses: pybind11 constructs the ALIAS payload on a subclass
            // shell only if it is constructible from const T&.
            static_assert(
                std::is_constructible_v<construction_type<T>, const T&>,
                "welder: this type's trampoline lacks a copy-from-base "
                "constructor, so a copied Python-subclass instance would hold a "
                "plain base payload and silently stop dispatching virtuals into "
                "Python. The WELDER_PY_TRAMPOLINE(TRAMP, BASE) macro declares "
                "it; a hand-rolled trampoline needs 'Tramp(const Base&)' — or "
                "mark::exclude the copy constructor.");
            // Registered BEFORE the user constructors: overloads are tried in
            // registration order, so a permissive user constructor (one taking
            // a generic object, a variant embedding T, …) cannot intercept a
            // T-instance argument — T(other) and the copy protocol always
            // reach the C++ copy constructor.
            cls.def(py::init<const T&>());
            cls.def("__copy__", [](py::handle self) {
                return _copy_instance<T>(self, nullptr);
            });
            cls.def(
                "__deepcopy__",
                [](py::handle self, py::object memo) {
                    return _copy_instance<T>(self, &memo);
                },
                py::arg("memo"));
        }
        template for (constexpr auto ctor : std::define_static_array(Ctors)) {
            _def_init<ctor>(cls, std::make_index_sequence<
                                     std::meta::parameters_of(ctor).size()>{});
        }
        if constexpr (Aggregate) {
            constexpr auto fields{::welder::detail::aggregate_fields<T>()};
            _def_aggregate_init<T>(cls, std::make_index_sequence<fields.size()>{});
        }
    }

    /** Bind data member @a Mem as an attribute.

        pybind11 data members are already Python properties (data descriptors on the
        class), so a `[[=welder::doc]]` on the member rides along as the property's
        `__doc__` — and thus reaches `.pyi` stubs. A const member is read-only
        (`def_readonly`); a mutable one is read/write (`def_readwrite`). The doc,
        when present, is passed as the property docstring. There is deliberately no
        setter docstring: a Python `property` surfaces only the getter's `__doc__`,
        so one would be pure overhead.
        @see welder::rod */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(auto& cls) {
        constexpr const char* name{
            ::welder::name_of<Mem, language, Style, ::welder::ent_kind::field>()};
        constexpr const char* doc{::welder::doc_of<Mem>()};
        if constexpr (!std::meta::is_public(Mem)) {
            // A protected member (admitted under policy::weld_protected) binds
            // as a property over welder::detail::field_access — gcc-16 rejects
            // the dependent `&[:Mem:]` for protected data (see field_access).
            // Same semantics as def_readwrite: reference_internal getter.
            using fa = ::welder::detail::field_access<Mem>;
            if constexpr (std::meta::is_const_type(std::meta::type_of(Mem))) {
                if constexpr (doc)
                    cls.def_property_readonly(
                        name, &fa::get, py::return_value_policy::reference_internal,
                        doc);
                else
                    cls.def_property_readonly(
                        name, &fa::get,
                        py::return_value_policy::reference_internal);
            } else {
                if constexpr (doc)
                    cls.def_property(name, &fa::get, &fa::set,
                                     py::return_value_policy::reference_internal,
                                     doc);
                else
                    cls.def_property(name, &fa::get, &fa::set,
                                     py::return_value_policy::reference_internal);
            }
        } else if constexpr (std::meta::is_const_type(std::meta::type_of(Mem))) {
            // const member: read-only (def_readwrite's setter would not compile).
            if constexpr (doc)
                cls.def_readonly(name, &[:Mem:], doc);
            else
                cls.def_readonly(name, &[:Mem:]);
        } else {
            if constexpr (doc)
                cls.def_readwrite(name, &[:Mem:], doc);
            else
                cls.def_readwrite(name, &[:Mem:]);
        }
    }

    /** Bind method overload group @a Fns (name from `Fns[0]`; pybind11 chains one
        `.def` per overload and dispatches at call time). @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_method(auto& cls) {
        constexpr const char* name{
            ::welder::name_of<Fns[0], language, Style, ::welder::ent_kind::method>()};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            _def_function<fn>(name, [&cls](auto&&... a) {
                cls.def(std::forward<decltype(a)>(a)...);
            });
        }
    }

    /** Bind static-method overload group @a Fns. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_static_method(auto& cls) {
        constexpr const char* name{
            ::welder::name_of<Fns[0], language, Style,
                              ::welder::ent_kind::static_method>()};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            _def_function<fn>(name, [&cls](auto&&... a) {
                cls.def_static(std::forward<decltype(a)>(a)...);
            });
        }
    }

    /** Bind operator slot group @a Fns — one (operator, arity) slot whole,
        member and anchored *free* entries mixed. A member (or @a T-on-the-left
        free) entry binds under the slot's dunder; a free entry with @a T as the
        RIGHT operand binds under the REFLECTED dunder (`__rmul__`, or the
        mirrored comparison) through an operand-swapping wrapper, so
        `2.0 * v` works from Python exactly as from C++. Binary
        arithmetic/comparison defs carry `py::is_operator()`: a failed operand
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

    /** Synthesize the relational dunders from `operator<=>` group @a Fns: for
        each spaceship overload's operand type, bind plain rewritten expressions
        (`a < b`, …) — C++'s own rewriting rules pick the right overload, so
        Python comparisons match C++ exactly, heterogeneous operands included
        (the reversed direction rides Python's reflected protocol:
        `5 < obj` → `obj.__gt__(5)`). A slot an explicit participating operator
        already covers is skipped (@a Covered — explicit beats synthesis).
        `__eq__` is never synthesized: C++ itself only rewrites `==` from
        `operator==`, and the member a *defaulted* spaceship implicitly declares
        binds through the ordinary operator path. @see welder::rod */
    template <class T, auto Fns, auto Covered>
    static void add_comparisons(auto& cls) {
        ::welder::rods::python::synthesize_comparisons<T, Fns, Covered>(
            [&cls](const char* name, auto fp) {
                cls.def(name, fp, py::is_operator{});
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
        dunder @a name. Unlike `_def_function`, never passes `py::arg` names —
        Python's operator protocol is positional-only (and pybind11 rejects
        per-parameter annotations on a free binary function bound as a method
        slot). Docstring, `return_policy` and `keep_alive`s ride along as usual;
        @a NotImpl appends `py::is_operator()` (see `add_operator`).
        @tparam Fn the operator. @tparam K the keep_alive index pack. */
    template <std::meta::info Fn, bool NotImpl, class Cls, std::size_t... K>
    static void _def_operator(const char* name, Cls& cls,
                              std::index_sequence<K...>) {
        static constexpr auto ka{::welder::detail::keep_alive_pairs<Fn>()};
        ::welder::validate_return_policy<Fn, language>();
        constexpr ::welder::rv_kind rvk{::welder::return_policy_of(Fn, language)};
        static_assert(rvk != ::welder::rv_kind::none,
                      "welder: return_policy 'none' has no pybind11 equivalent "
                      "(it is nanobind-only) — choose another policy");
        constexpr py::return_value_policy rvp{_return_value_policy(rvk)};
        const std::string doc{::welder::function_docstring<Fn, DocStyle>()};
        auto def{[&](auto&&... extra) {
            if (doc.empty())
                cls.def(name, &[:Fn:], rvp,
                        py::keep_alive<ka[K].nurse, ka[K].patient>()...,
                        std::forward<decltype(extra)>(extra)...);
            else
                cls.def(name, &[:Fn:], doc.c_str(), rvp,
                        py::keep_alive<ka[K].nurse, ka[K].patient>()...,
                        std::forward<decltype(extra)>(extra)...);
        }};
        if constexpr (NotImpl)
            def(py::is_operator{});
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
                py::is_operator{});
        }
    }

  public:

    // --- enum binding -------------------------------------------------------

    /** Owning handle for a `py::native_enum<E>`, plus the scope + name to reach the
        finalized enum object.

        `native_enum` binds Python's stdlib `enum.IntEnum`, so enumerators are
        int-convertible (`int(E.Value)`) and compare against ints — matching
        pybind11's legacy `py::enum_` default and the nanobind backend's
        `nb::is_arithmetic()`. We use `native_enum` because plain `py::enum_` is
        discouraged as of pybind11 3.0.

        `native_enum` is move-only and must be explicitly `.finalize()`d, so it is
        wrapped here in a movable handle the shared `bind_enum` driver can hold by
        value, drive via `value()`/`export_values()`, and finalize at the end. */
    template <class E>
    struct enum_handle {
        py::object scope; ///< the enclosing scope: a (sub)module, or — for a
                          ///< nested enum — the enclosing class handle
        const char* name;                         ///< the enum's Python name
        std::unique_ptr<py::native_enum<E>> impl; ///< the (move-only) native enum

        /** Add enumerator @a n = @a v to the pending native enum.
            @param n the enumerator's Python name.
            @param v the enumerator value. */
        void value(const char* n, E v) { impl->value(n, v); }
        /** Export the enumerators into the enclosing scope (unscoped enums). */
        void export_values() { impl->export_values(); }

        /** Commit the enum onto @ref scope as @ref name, and stamp the
            pybind11-stubgen native-enum marker. */
        void finalize() {
            impl->finalize(); // commits the enum onto `scope` as `name`
            // pybind11 3.0.1 does not yet stamp the `__pybind11_native_enum__`
            // marker that pybind11-stubgen keys on to recognize a stdlib-enum (and
            // strip its `enum` internals from the generated .pyi); a later pybind11
            // sets it. Set it ourselves so welded enums produce clean stubs — both
            // welder's own and any run by a consumer. Harmless once pybind11 sets it
            // too; drop when the conan pybind11 package carries it.
            scope.attr(name).attr("__pybind11_native_enum__") = true;
        }
    };

    /** The class / enum handles the per-class / per-enum hooks operate on — exactly what
        `make_class` / `make_enum` yield. `class_handle_type<T>` tracks `make_class`
        itself (so it captures the woven-in trampoline for a virtual `T`); it is its
        return type for a **base-less** `T` — welded bases, chosen by the carriage's
        *resolution* rather than by `T`, are appended by `make_class` and so cannot be a
        function of `T` alone. Named as associated types so the @ref welder::rod concept
        can shape-check the per-handle hooks against them. (Co-located with `enum_handle`,
        its definition.) */
    template <class T>
    using class_handle_type = decltype(make_class<T, std::array<std::meta::info, 0>{}>(
        std::declval<module_type&>(), nullptr, nullptr, std::index_sequence<>{}));
    template <class E> using enum_handle_type = enum_handle<E>;

    /** Create the `enum_handle` for @a E (a non-null @a doc becomes its docstring).
        @see welder::rod */
    template <class E>
    static enum_handle<E> make_enum(module_type& m, const char* name,
                                    const char* doc) {
        // native_enum's class_doc "" means "leave __doc__ untouched"; a non-null doc
        // is the enum docstring, applied at finalize().
        return {m, name,
                std::make_unique<py::native_enum<E>>(m, name, "enum.IntEnum",
                                                     doc ? doc : "")};
    }

    /** Create the `enum_handle` for a **nested** member enum @a E, scoped to its
        enclosing type's class handle — Python sees `module.Outer.Mode`, and an
        *unscoped* nested enum's `export_values()` lands its enumerators on the
        class (mirroring C++'s `Outer::red`). @see welder::rod */
    template <class E>
    static enum_handle<E> make_nested_enum(module_type&, auto& outer_cls,
                                           const char* name, const char* doc) {
        return {outer_cls, name,
                std::make_unique<py::native_enum<E>>(outer_cls, name,
                                                     "enum.IntEnum",
                                                     doc ? doc : "")};
    }

    /** Add enumerator @a Enum to the enum handle. @see welder::rod */
    template <std::meta::info Enum, class Style = ::welder::naming::none>
    static void add_enumerator(auto& e) {
        e.value(
            ::welder::name_of<Enum, language, Style, ::welder::ent_kind::enumerator>(),
            [:Enum:]);
    }

    /** Finalize enum @a E: export an unscoped enum's values into the enclosing scope,
        then commit the enum to the module. @see welder::rod */
    template <class E>
    static void finish_enum(auto& e) {
        // Mirror C++ scope semantics: an unscoped enum's enumerators are visible
        // unqualified, so export them into the enclosing scope; a scoped enum's
        // are reached as E.Value, so leave them scoped.
        if constexpr (!std::is_scoped_enum_v<E>)
            e.export_values();
        e.finalize(); // native_enum requires an explicit finalize()
    }

    // --- namespace / module binding -----------------------------------------

    /** Open a per-module session: a dict accumulating live (mutable-variable)
        properties; _install_live_properties() applies them in one `__class__` swap
        at close. @see welder::rod */
    static py::dict open_module(module_type&) { return py::dict{}; }

    /** Set the (sub)module docstring. @see welder::rod */
    static void set_module_doc(module_type& m, const char* doc) { m.doc() = doc; }

    /** Bind free-function overload group @a Fns as one module-level function
        (name from `Fns[0]`; one chained `.def` per overload).

        A non-null @a name overrides the resolved name (including any `weld_as`),
        used verbatim; `nullptr` falls back to the styled/`weld_as` name.
        @return the bound function object (`m.attr(name)`) — the handle for
                further hand-registration. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static py::object add_function(module_type& m, const char* name = nullptr) {
        const char* fn_name{::welder::name_of_or<Fns[0], language, Style,
                                                 ::welder::ent_kind::function>(name)};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            _def_function<fn>(fn_name, [&m](auto&&... a) {
                m.def(std::forward<decltype(a)>(a)...);
            });
        }
        return m.attr(fn_name);
    }

    /** Bind namespace variable @a Var as a module attribute.

        A const/constexpr variable becomes a value snapshot; a mutable one becomes a
        live get/set property over the C++ global (accumulated in @a live). A non-null
        @a name_override is used verbatim (beating any `weld_as`); `nullptr` falls back
        to the styled/`weld_as` name. @see welder::rod */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, py::dict& live,
                             const char* name_override = nullptr) {
        const char* name{
            ::welder::name_of_or<Var, language, Style, ::welder::ent_kind::variable>(
                name_override)};
        if constexpr (std::meta::is_const_type(std::meta::type_of(Var))) {
            m.attr(name) = [:Var:]; // immutable: a value snapshot at bind time
        } else {
            // Mutable: a live property over the C++ global. The descriptors take a
            // leading `self` (the module) and ignore it.
            auto property{py::module_::import("builtins").attr("property")};
            live[name] = property(
                py::cpp_function([](py::object) { return [:Var:]; }),
                py::cpp_function([](py::object,
                                    typename [:std::meta::type_of(Var):] v) {
                    [:Var:] = v;
                }));
        }
    }

    /** Create a submodule named @a name under @a m. @see welder::rod */
    static module_type add_submodule(module_type& m, const char* name) {
        return m.def_submodule(name);
    }

    /** Close the session: apply any accumulated live properties. @see welder::rod */
    static void close_module(module_type& m, py::dict& live) {
        if (live.size() != 0)
            _install_live_properties(m, live);
    }
};

static_assert(::welder::rod<rod<>>,
              "welder::rods::pybind11::rod<> must satisfy welder::rod");

} // namespace welder::rods::pybind11
