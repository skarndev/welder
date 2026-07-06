#pragma once
/** @file
    welder pybind11 rod (header-only).

    This is a *thin* rod: it implements welder's rod contract
    (`<welder/welder.hpp>`) for pybind11 and hands the traversal/resolution off to
    welder's generic driver. All the language-agnostic work — deciding which
    members bind, gating bindability, folding docstrings, walking bases and
    namespaces — lives in the core; only the pybind11 emission primitives are here.
    The nanobind / lua backends mirror this file against their own frameworks.

    Requires the welder vocabulary to be available first, via either `import
    welder;` (module form) or `#include <welder/vocabulary.hpp>` (header-only).
    This header exposes exactly one thing: the rod type
    `welder::rods::pybind11::rod`, to plug into `welder::welder`:
    @code
    #include <pybind11/pybind11.h>
    #include <welder/rods/python/pybind11/rod.hpp>
    PYBIND11_MODULE(mymod, m) {
        welder::welder<welder::rods::pybind11::rod>::weld_type<MyType>(m);
    }
    @endcode
    (For the `WELDER_MODULE` entry-point macro, include this directory's
    `module.hpp` instead.)
*/
#include <array>
#include <cstddef>
#include <memory>
#include <meta>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/welder.hpp>     // welder::welder + the rod contract + driver
#include <welder/rods/python/doc_style.hpp> // welder::rods::python::google_style
#include <welder/rods/python/operators.hpp> // welder::rods::python::operator_dunder
#include <welder/bind_traits.hpp>// param_types / param_names / aggregate_fields
#include <welder/doc.hpp>        // function_docstring

#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h> // py::native_enum (stdlib enum binding)

namespace welder::rods::pybind11 {

// Inside `welder::rods::pybind11`, the unqualified name `pybind11` resolves to *this*
// namespace, not the library. Alias the real one once — the way pybind11's own
// docs spell it — and use `py::` for every library reference below.
namespace py = ::pybind11;

/** The pybind11 rod: a stateless policy type satisfying @ref welder::rod.

    Its public static members are the pybind11 emission primitives welder's driver
    calls; the driver supplies all the reflection-derived decisions. See
    `<welder/welder.hpp>` for the contract each member fulfills. The `protected`
    members below are pybind11-specific implementation helpers (prefixed `_`), not
    part of the contract.
*/
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

        @tparam T the type whose caster to classify.
    */
    template <class T>
    static constexpr bool _needs_registration =
        std::is_base_of_v<py::detail::type_caster_base<py::detail::intrinsic_t<T>>,
                          py::detail::make_caster<T>>;

    /** Register the function/method reflected by @a Fn onto a pybind11 target.

        The folded docstring is passed when non-empty, and `py::arg(name)...` when
        every parameter is named (so Python callers see real keyword arguments, not
        `arg0`/`arg1`).
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
            ::welder::function_docstring<Fn, ::welder::rods::python::google_style>()};
        if constexpr (::welder::detail::all_params_named<Fn>()) {
            if (doc.empty())
                def_into(name, &[:Fn:], py::arg(names[I])...);
            else
                def_into(name, &[:Fn:], doc.c_str(), py::arg(names[I])...);
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

        Reassigns @a m's Python class to a fresh `ModuleType` subclass carrying
        @a props (name → property). Python modules don't support properties
        directly, but a module's `__class__` may be swapped for a `ModuleType`
        subclass. Used only when a namespace exposes a mutable variable.
        @param m     the module handle.
        @param props a dict of name → `property`.
    */
    static void _install_live_properties(py::module_& m, py::dict props) {
        auto builtins{py::module_::import("builtins")};
        auto types{py::module_::import("types")};
        auto subclass{builtins.attr("type")(
            py::str("welder_live_module"),
            py::make_tuple(types.attr("ModuleType")), props)};
        m.attr("__class__") = subclass;
    }

    /** Construct `py::class_<T, NativeBases...>` from a reflected base-type array.

        A non-null @a doc becomes the class docstring (pybind11 treats a bare
        `const char*` extra as the doc); `nullptr` is branched out rather than
        passed, since pybind11 would `strdup` it.
        @tparam T     the class type.
        @tparam Bases the static array of native base type reflections.
        @tparam I     the base index pack.
        @param m    the module handle.
        @param name the Python class name.
        @param doc  the class docstring, or `nullptr`.
    */
    template <class T, auto Bases, std::size_t... I>
    static auto _make_class(py::module_& m, const char* name, const char* doc,
                            std::index_sequence<I...>) {
        if (doc)
            return py::class_<T, typename [:Bases[I]:]...>(m, name, doc);
        return py::class_<T, typename [:Bases[I]:]...>(m, name);
    }

  public:
    // --- caster oracle + emission primitives (the welder::rod contract) --

    /** `caster_oracle`: @a T is convertible without welder registering a class for
        it iff pybind11 does *not* fall back to runtime class registration. */
    template <class T>
    static constexpr bool has_native_caster = !_needs_registration<T>;

    /** Map a member operator to its Python dunder (`nullptr` = not exposed). */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return ::welder::rods::python::operator_dunder(op_fn);
    }

    // --- class binding ------------------------------------------------------

    /** Create the `py::class_<T, Bases…>` handle. @see _make_class */
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* doc,
                           std::index_sequence<I...> seq) {
        return _make_class<T, Bases>(m, name, doc, seq);
    }

    /** Bind the default constructor. */
    static void add_default_ctor(auto& cls) { cls.def(py::init<>()); }

    /** Bind constructor @a Ctor as a `py::init<…>`. @see _def_init */
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

        pybind11 data members are already Python properties (data descriptors on the
        class), so a `[[=welder::doc]]` on the member rides along as the property's
        `__doc__` — and thus reaches `.pyi` stubs. A const member is read-only
        (`def_readonly`); a mutable one is read/write (`def_readwrite`). The doc,
        when present, is passed as the property docstring. There is deliberately no
        setter docstring: a Python `property` surfaces only the getter's `__doc__`,
        so one would be pure overhead. */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(auto& cls) {
        constexpr const char* name{
            ::welder::name_of<Mem, language, Style, ::welder::ent_kind::field>()};
        constexpr const char* doc{::welder::doc_of<Mem>()};
        if constexpr (std::meta::is_const_type(std::meta::type_of(Mem))) {
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
        module_type scope;                        ///< the enclosing (sub)module
        const char* name;                         ///< the enum's Python name
        std::unique_ptr<py::native_enum<E>> impl; ///< the (move-only) native enum

        void value(const char* n, E v) { impl->value(n, v); }
        void export_values() { impl->export_values(); }

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

    /** Create the `enum_handle` for @a E (a non-null @a doc becomes its docstring). */
    template <class E>
    static enum_handle<E> make_enum(module_type& m, const char* name,
                                    const char* doc) {
        // native_enum's class_doc "" means "leave __doc__ untouched"; a non-null doc
        // is the enum docstring, applied at finalize().
        return {m, name,
                std::make_unique<py::native_enum<E>>(m, name, "enum.IntEnum",
                                                     doc ? doc : "")};
    }

    /** Add enumerator @a Enum to the enum handle. */
    template <std::meta::info Enum, class Style = ::welder::naming::none>
    static void add_enumerator(auto& e) {
        e.value(
            ::welder::name_of<Enum, language, Style, ::welder::ent_kind::enumerator>(),
            [:Enum:]);
    }

    /** Finalize enum @a E: export an unscoped enum's values into the enclosing scope,
        then commit the enum to the module. */
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
        at close. */
    static py::dict open_module(module_type&) { return py::dict{}; }

    /** Set the (sub)module docstring. */
    static void set_module_doc(module_type& m, const char* doc) { m.doc() = doc; }

    /** Bind free function @a Fn as a module-level function. */
    template <std::meta::info Fn, class Style = ::welder::naming::none>
    static void add_function(module_type& m) {
        _def_function<Fn>(
            ::welder::name_of<Fn, language, Style, ::welder::ent_kind::function>(),
            [&m](auto&&... a) { m.def(std::forward<decltype(a)>(a)...); });
    }

    /** Bind namespace variable @a Var as a module attribute.

        A const/constexpr variable becomes a value snapshot; a mutable one becomes a
        live get/set property over the C++ global (accumulated in @a live). */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, py::dict& live) {
        constexpr const char* name{
            ::welder::name_of<Var, language, Style, ::welder::ent_kind::variable>()};
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

    /** Create a submodule named @a name under @a m. */
    static module_type add_submodule(module_type& m, const char* name) {
        return m.def_submodule(name);
    }

    /** Close the session: apply any accumulated live properties. */
    static void close_module(module_type& m, py::dict& live) {
        if (live.size() != 0)
            _install_live_properties(m, live);
    }
};

static_assert(::welder::rod<rod>,
              "welder::rods::pybind11::rod must satisfy welder::rod");

} // namespace welder::rods::pybind11
