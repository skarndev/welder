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
#include <meta>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/welder.hpp>     // welder::welder + the rod contract + driver
#include <welder/rods/python/doc_style.hpp> // welder::rods::python::google_style
#include <welder/rods/python/operators.hpp> // welder::rods::python::operator_dunder
#include <welder/bind_traits.hpp>// param_types / param_names / aggregate_fields
#include <welder/doc.hpp>        // function_docstring

#include <nanobind/nanobind.h>

namespace welder::rods::nanobind {

// Inside `welder::rods::nanobind`, the unqualified name `nanobind` resolves to *this*
// namespace, not the library. Alias the real one once — the way nanobind's own
// docs spell it — and use `nb::` for every library reference below.
namespace nb = ::nanobind;

/** The nanobind rod: a stateless policy type satisfying @ref welder::rod.

    Its public static members are the nanobind emission primitives welder's driver
    calls; the driver supplies all the reflection-derived decisions. See
    `<welder/welder.hpp>` for the contract each member fulfills. The `protected`
    members below are nanobind-specific implementation helpers (prefixed `_`), not
    part of the contract.

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
        their `<nanobind/stl/*>` converter header is included). A type hand-registered
        out-of-band still reads `true`; that false positive is resolved by the
        deferred `trust_bindable` escape hatch.

        @tparam T the type whose caster to classify.
    */
    template <class T>
    static constexpr bool _needs_registration =
        nb::detail::is_base_caster_v<nb::detail::make_caster<T>>;

    /** Register the function/method reflected by @a Fn onto a nanobind target.

        The folded docstring is passed when non-empty, and `nb::arg(name)...` when
        every parameter is named (so Python callers see real keyword arguments, not
        positional-only).
        @tparam Fn a reflection of the function.
        @tparam Def the target-adapter callable type.
        @tparam I the parameter index pack.
        @param name     the Python name.
        @param def_into adapts the target — `cls.def`, `cls.def_static`, or `m.def`.
    */
    template <std::meta::info Fn, class Def, std::size_t... I>
    static void _def_function(const char* name, Def def_into,
                              std::index_sequence<I...>) {
        static constexpr auto names{::welder::detail::param_names<Fn>()};
        const std::string doc{
            ::welder::function_docstring<Fn, DocStyle>()};
        if constexpr (::welder::detail::all_params_named<Fn>()) {
            if (doc.empty())
                def_into(name, &[:Fn:], nb::arg(names[I])...);
            else
                def_into(name, &[:Fn:], doc.c_str(), nb::arg(names[I])...);
        } else {
            if (doc.empty())
                def_into(name, &[:Fn:]);
            else
                def_into(name, &[:Fn:], doc.c_str());
        }
    }

    /** Convenience overload: derive the parameter index sequence from @a Fn. */
    template <std::meta::info Fn, class Def>
    static void _def_function(const char* name, Def def_into) {
        _def_function<Fn>(name, def_into,
                          std::make_index_sequence<
                              std::meta::parameters_of(Fn).size()>{});
    }

    /** Register `nb::init<P0, P1, …>()` for constructor @a Ctor.

        Names the parameters (`nb::arg`) when all are named, otherwise positional.
        @tparam Ctor a reflection of the constructor.
        @tparam I the parameter index pack.
        @param cls the class handle.
    */
    template <std::meta::info Ctor, std::size_t... I>
    static void _def_init(auto& cls, std::index_sequence<I...>) {
        static constexpr auto params{::welder::detail::param_types<Ctor>()};
        static constexpr auto names{::welder::detail::param_names<Ctor>()};
        if constexpr (::welder::detail::all_params_named<Ctor>())
            cls.def(nb::init<typename [:params[I]:]...>(), nb::arg(names[I])...);
        else
            cls.def(nb::init<typename [:params[I]:]...>());
    }

    /** Synthesize a field constructor for a baseless aggregate @a T.

        nanobind builds a custom constructor by binding an `__init__` whose first
        parameter is a pointer to the (uninitialized) instance; the body
        placement-news the aggregate from the field values. Emits
        `def("__init__", [](T* self, F0 f0, …) { new (self) T{f0, …}; },
        nb::arg("f0"), …)` so Python can build it from field values (`T(f0, f1)`).
        @tparam T the aggregate type.
        @tparam I the field index pack.
        @param cls the class handle.
    */
    template <class T, std::size_t... I>
    static void _def_aggregate_init(auto& cls, std::index_sequence<I...>) {
        static constexpr auto fields{::welder::detail::aggregate_fields<T>()};
        cls.def(
            "__init__",
            [](T* self, typename [:std::meta::type_of(fields[I]):]... args) {
                new (self) T{std::move(args)...};
            },
            nb::arg(std::define_static_string(
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
        @param m    the module handle.
        @param name the Python class name.
        @param doc  the class docstring, or `nullptr`.
    */
    template <class T, auto Bases, std::size_t... I>
    static auto _make_class(nb::module_& m, const char* name, const char* doc,
                            std::index_sequence<I...>) {
        if (doc)
            return nb::class_<T, typename [:Bases[I]:]...>(m, name, doc);
        return nb::class_<T, typename [:Bases[I]:]...>(m, name);
    }

  public:
    // --- caster oracle + emission primitives (the welder::rod contract) --

    /** `caster_oracle`: @a T is convertible without welder registering a class for
        it iff nanobind does *not* fall back to runtime class registration. */
    template <class T>
    static constexpr bool has_native_caster = !_needs_registration<T>;

    /** Map a member operator to its Python dunder (`nullptr` = not exposed). */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return ::welder::rods::python::operator_dunder(op_fn);
    }

    // --- class binding ------------------------------------------------------

    /** Create the `nb::class_<T, Bases…>` handle. @see _make_class */
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* doc,
                           std::index_sequence<I...> seq) {
        return _make_class<T, Bases>(m, name, doc, seq);
    }

    /** Bind the default constructor. */
    static void add_default_ctor(auto& cls) { cls.def(nb::init<>()); }

    /** Bind constructor @a Ctor as an `nb::init<…>`. @see _def_init */
    template <std::meta::info Ctor>
    static void add_constructor(auto& cls) {
        _def_init<Ctor>(cls, std::make_index_sequence<
                                 std::meta::parameters_of(Ctor).size()>{});
    }

    /** Bind the synthesized aggregate field constructor. @see _def_aggregate_init */
    template <class T>
    static void add_aggregate_constructor(auto& cls) {
        constexpr auto fields{::welder::detail::aggregate_fields<T>()};
        _def_aggregate_init<T>(cls, std::make_index_sequence<fields.size()>{});
    }

    /** Bind data member @a Mem as an attribute.

        nanobind data members become Python properties (data descriptors on the
        class), so a `[[=welder::doc]]` on the member rides along as the property's
        `__doc__` — and thus reaches `.pyi` stubs. A const member is read-only
        (`def_ro`); a mutable one is read/write (`def_rw`). The doc, when present, is
        passed as the property docstring. There is deliberately no setter docstring:
        a Python `property` surfaces only the getter's `__doc__`. */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(auto& cls) {
        constexpr const char* name{
            ::welder::name_of<Mem, language, Style, ::welder::ent_kind::field>()};
        constexpr const char* doc{::welder::doc_of<Mem>()};
        if constexpr (std::meta::is_const_type(std::meta::type_of(Mem))) {
            // const member: read-only (def_rw's setter would not compile).
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

    /** Bind member function @a Fn as a method. */
    template <std::meta::info Fn, class Style = ::welder::naming::none>
    static void add_method(auto& cls) {
        _def_function<Fn>(
            ::welder::name_of<Fn, language, Style, ::welder::ent_kind::method>(),
            [&cls](auto&&... a) { cls.def(std::forward<decltype(a)>(a)...); });
    }

    /** Bind static member function @a Fn as a static method. */
    template <std::meta::info Fn, class Style = ::welder::naming::none>
    static void add_static_method(auto& cls) {
        _def_function<Fn>(
            ::welder::name_of<Fn, language, Style, ::welder::ent_kind::static_method>(),
            [&cls](auto&&... a) { cls.def_static(std::forward<decltype(a)>(a)...); });
    }

    /** Bind member operator @a Fn under its Python dunder. */
    template <std::meta::info Fn>
    static void add_operator(auto& cls) {
        _def_function<Fn>(::welder::rods::python::operator_dunder(Fn),
                          [&cls](auto&&... a) {
                              cls.def(std::forward<decltype(a)>(a)...);
                          });
    }

    // --- enum binding -------------------------------------------------------

    /** Create the `nb::enum_<E>` handle (a non-null @a doc becomes its docstring).

        `nb::is_arithmetic()` makes it a Python `enum.IntEnum`, so enumerators are
        int-convertible (`int(E.Value)`) and compare against ints — matching the
        pybind11 backend, whose `py::native_enum` also binds an `enum.IntEnum`. */
    template <class E>
    static auto make_enum(module_type& m, const char* name, const char* doc) {
        // A non-null doc is the enum docstring (a bare const char* extra); nullptr
        // is branched out rather than passed.
        if (doc)
            return nb::enum_<E>(m, name, doc, nb::is_arithmetic());
        return nb::enum_<E>(m, name, nb::is_arithmetic());
    }

    /** Add enumerator @a Enum to the enum handle. */
    template <std::meta::info Enum, class Style = ::welder::naming::none>
    static void add_enumerator(auto& e) {
        e.value(
            ::welder::name_of<Enum, language, Style, ::welder::ent_kind::enumerator>(),
            [:Enum:]);
    }

    /** Finalize enum @a E: export an unscoped enum's values into the enclosing scope. */
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
        at close. */
    static nb::dict open_module(module_type&) { return nb::dict{}; }

    /** Set the (sub)module docstring.

        nanobind's `module_` exposes no `doc()` setter, so the docstring is written
        straight to the module's `__doc__` attribute. */
    static void set_module_doc(module_type& m, const char* doc) {
        m.attr("__doc__") = doc;
    }

    /** Bind free function @a Fn as a module-level function.

        A non-null @a name overrides the resolved name (including any `weld_as`),
        used verbatim; `nullptr` falls back to the styled/`weld_as` name. */
    template <std::meta::info Fn, class Style = ::welder::naming::none>
    static void add_function(module_type& m, const char* name = nullptr) {
        _def_function<Fn>(
            name ? name
                 : ::welder::name_of<Fn, language, Style, ::welder::ent_kind::function>(),
            [&m](auto&&... a) { m.def(std::forward<decltype(a)>(a)...); });
    }

    /** Bind namespace variable @a Var as a module attribute.

        A const/constexpr variable becomes a value snapshot; a mutable one becomes a
        live get/set property over the C++ global (accumulated in @a live). A non-null
        @a name_override is used verbatim (beating any `weld_as`); `nullptr` falls back
        to the styled/`weld_as` name. */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, nb::dict& live,
                             const char* name_override = nullptr) {
        const char* name{
            name_override
                ? name_override
                : ::welder::name_of<Var, language, Style, ::welder::ent_kind::variable>()};
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

    /** Create a submodule named @a name under @a m. */
    static module_type add_submodule(module_type& m, const char* name) {
        return m.def_submodule(name);
    }

    /** Close the session: apply any accumulated live properties. */
    static void close_module(module_type& m, nb::dict& live) {
        if (live.size() != 0)
            _install_live_properties(m, live);
    }
};

static_assert(::welder::rod<rod<>>,
              "welder::rods::nanobind::rod<> must satisfy welder::rod");

} // namespace welder::rods::nanobind
