#pragma once
/** @file
    welder pybind11 backend (header-only).

    This is a *thin* backend: it implements welder's backend contract
    (`<welder/backend.hpp>`) for pybind11 and hands the traversal/resolution off to
    welder's generic driver. All the language-agnostic work — deciding which
    members bind, gating bindability, folding docstrings, walking bases and
    namespaces — lives in the core; only the pybind11 emission primitives are here.
    A future nanobind / lua backend mirrors this file against its own framework.

    Requires the welder vocabulary to be available first, via either `import
    welder;` (module form) or `#include <welder/welder.hpp>` (header-only). Then:
    @code
    #include <pybind11/pybind11.h>
    #include <welder/backends/python/pybind11/backend.hpp>
    PYBIND11_MODULE(mymod, m) { welder::pybind11::bind<MyType>(m); }
    @endcode
*/
#include <array>
#include <cstddef>
#include <meta>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/backend.hpp>    // the backend contract + generic driver
#include <welder/backends/python/doc_style.hpp> // welder::python::google_style
#include <welder/bind_traits.hpp>// param_types / param_names / aggregate_fields
#include <welder/doc.hpp>        // function_docstring
#include <welder/module.hpp>     // WELDER_MODULE dispatch (entry-point macro)

#include <pybind11/pybind11.h>

namespace welder::pybind11 {

// Inside `welder::pybind11`, the unqualified name `pybind11` resolves to *this*
// namespace, not the library. Alias the real one once — the way pybind11's own
// docs spell it — and use `py::` for every library reference below.
namespace py = ::pybind11;

namespace detail {

/** Whether pybind11 can only convert @a T via *runtime class registration*.

    True iff @a T's caster is (or derives from) the generic `type_caster_base`
    fallback, which looks @a T up in pybind11's registered-types map. True for
    program-defined classes and enums; false for scalars, strings and the pybind11
    wrapper types (`py::object`, `py::dict`, …). This is the one bindability fact
    welder's core cannot know on its own; it drives `caster_oracle` below.

    Two things it deliberately does NOT see through — both making it
    *conservative* (it may over-report needs-registration, never under-report):
    - It reads @a T's *caster type* at compile time, so it reports whether @a T
      *needs* a `class_`/`enum_` — never whether one will actually exist at
      runtime. A class the user hand-registers with `py::class_` (or a third-party
      library registers) but does not weld still reads `true`, so welder requires
      `welded_for` and rejects it: a false positive, resolved by the deferred
      `trust_bindable` escape hatch, since the out-of-band registration is
      invisible here.
    - "native" is relative to the TU's includes: `std::complex` / `std::function` /
      `std::chrono` / `std::filesystem::path` are native only when their converter
      header (`<pybind11/complex.h>`, `functional.h`, `chrono.h`,
      `stl/filesystem.h`) is included; otherwise they fall to the class-registration
      fallback. That is correct — without the header pybind11 genuinely cannot
      convert them.

    A user `type_caster` is trusted (reads `false`) only when it is *self-contained*
    — it does not itself derive from `type_caster_base` (e.g. a
    `PYBIND11_TYPE_CASTER` caster). One that derives from `type_caster_base` still
    needs @a T registered, so it correctly still reads `true`.

    @tparam T the type whose caster to classify.
*/
template <class T>
inline constexpr bool needs_registration =
    std::is_base_of_v<py::detail::type_caster_base<py::detail::intrinsic_t<T>>,
                      py::detail::make_caster<T>>;

/** The Python special-method ("dunder") name for a member operator, or `nullptr`
    if welder does not expose that operator.

    Unary vs binary is told apart by arity (a member operator takes 0 parameters
    when unary, 1 when binary), disambiguating the operators that have both forms
    (`+`, `-`). In-place compound assignments (`operator+=`, …) are intentionally
    not mapped: Python already falls back to the binary form
    (`a += b` → `a = a + b` via `__add__`) with correct value semantics, and
    binding `__iadd__` faithfully would need a reference return policy. Likewise
    `<=>`, `&&`, `||`, `++`, `--` and `=` have no clean reflection-driven Python
    mapping yet.

    @param f a reflection of the operator function.
    @return the dunder name (static storage), or `nullptr`.
*/
consteval const char* operator_dunder_of(std::meta::info f) {
    using std::meta::operators;
    const bool unary{welder::detail::is_unary_operator(f)};
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:    return unary ? "__pos__" : "__add__";
        case operators::op_minus:   return unary ? "__neg__" : "__sub__";
        case operators::op_star:    return unary ? nullptr : "__mul__"; // unary * = deref
        case operators::op_slash:   return "__truediv__";
        case operators::op_percent: return "__mod__";
        case operators::op_tilde:   return "__invert__";
        case operators::op_caret:   return "__xor__";
        case operators::op_ampersand: return unary ? nullptr : "__and__"; // unary & = address-of
        case operators::op_pipe:    return "__or__";
        case operators::op_less_less:       return "__lshift__";
        case operators::op_greater_greater: return "__rshift__";
        case operators::op_equals_equals:      return "__eq__";
        case operators::op_exclamation_equals: return "__ne__";
        case operators::op_less:           return "__lt__";
        case operators::op_greater:        return "__gt__";
        case operators::op_less_equals:    return "__le__";
        case operators::op_greater_equals: return "__ge__";
        case operators::op_parentheses:     return "__call__";
        case operators::op_square_brackets: return "__getitem__";
        default:                            return nullptr;
    }
}

/** Register the function/method reflected by @a Fn onto a pybind11 target.

    The folded docstring is passed when non-empty, and `py::arg(name)...` when every
    parameter is named (so Python callers see real keyword arguments, not
    `arg0`/`arg1`).
    @tparam Fn a reflection of the function.
    @tparam Def the target-adapter callable type.
    @tparam I the parameter index pack.
    @param name     the Python name.
    @param def_into adapts the target — `cls.def`, `cls.def_static`, or `m.def`.
*/
template <std::meta::info Fn, class Def, std::size_t... I>
void def_function(const char* name, Def def_into, std::index_sequence<I...>) {
    static constexpr auto names{welder::detail::param_names<Fn>()};
    const std::string doc{
        welder::function_docstring<Fn, welder::python::google_style>()};
    if constexpr (welder::detail::all_params_named<Fn>()) {
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
void def_function(const char* name, Def def_into) {
    def_function<Fn>(name, def_into,
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
void def_init(auto& cls, std::index_sequence<I...>) {
    static constexpr auto params{welder::detail::param_types<Ctor>()};
    static constexpr auto names{welder::detail::param_names<Ctor>()};
    if constexpr (welder::detail::all_params_named<Ctor>())
        cls.def(py::init<typename [:params[I]:]...>(), py::arg(names[I])...);
    else
        cls.def(py::init<typename [:params[I]:]...>());
}

/** Synthesize a field constructor for a baseless aggregate @a T.

    Emits `py::init([](F0 f0, …) { return T{f0, …}; }, py::arg("f0"), …)` so Python
    can build it from field values (`T(f0, f1)`) rather than only default-construct
    then assign.
    @tparam T the aggregate type.
    @tparam I the field index pack.
    @param cls the class handle.
*/
template <class T, std::size_t... I>
void def_aggregate_init(auto& cls, std::index_sequence<I...>) {
    static constexpr auto fields{welder::detail::aggregate_fields<T>()};
    cls.def(py::init([](typename [:std::meta::type_of(fields[I]):]... args) {
                return T{std::move(args)...};
            }),
            py::arg(std::define_static_string(
                std::meta::identifier_of(fields[I])))...);
}

/** Give module @a m live get/set semantics for the names in @a props.

    Reassigns @a m's Python class to a fresh `ModuleType` subclass carrying @a props
    (name → property). Python modules don't support properties directly, but a
    module's `__class__` may be swapped for a `ModuleType` subclass. Used only when
    a namespace exposes a mutable variable.
    @param m     the module handle.
    @param props a dict of name → `property`.
*/
inline void install_live_properties(py::module_& m, py::dict props) {
    auto builtins{py::module_::import("builtins")};
    auto types{py::module_::import("types")};
    auto subclass{builtins.attr("type")(
        py::str("welder_live_module"),
        py::make_tuple(types.attr("ModuleType")), props)};
    m.attr("__class__") = subclass;
}

/** Construct `py::class_<T, NativeBases...>` from a reflected base-type array.

    A non-null @a doc becomes the class docstring (pybind11 treats a bare
    `const char*` extra as the doc); `nullptr` is branched out rather than passed,
    since pybind11 would `strdup` it.
    @tparam T     the class type.
    @tparam Bases the static array of native base type reflections.
    @tparam I     the base index pack.
    @param m    the module handle.
    @param name the Python class name.
    @param doc  the class docstring, or `nullptr`.
*/
template <class T, auto Bases, std::size_t... I>
auto make_class_impl(py::module_& m, const char* name, const char* doc,
                     std::index_sequence<I...>) {
    if (doc)
        return py::class_<T, typename [:Bases[I]:]...>(m, name, doc);
    return py::class_<T, typename [:Bases[I]:]...>(m, name);
}

/** The pybind11 backend: a stateless policy type satisfying @ref welder::backend.

    Its static members are the pybind11 emission primitives welder's driver calls;
    the driver supplies all the reflection-derived decisions. See
    `<welder/backend.hpp>` for the contract each member fulfills.
*/
struct backend {
    static constexpr lang language{lang::py}; /**< welder::lang::py. */
    using module_type = py::module_;          /**< pybind11's module handle. */

    /** `caster_oracle`: @a T is convertible without welder registering a class for
        it iff pybind11 does *not* fall back to runtime class registration. */
    template <class T>
    static constexpr bool has_native_caster = !needs_registration<T>;

    /** Map a member operator to its Python dunder (`nullptr` = not exposed). */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return operator_dunder_of(op_fn);
    }

    // --- class binding ------------------------------------------------------

    /** Create the `py::class_<T, Bases…>` handle. @see make_class_impl */
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* doc,
                           std::index_sequence<I...> seq) {
        return make_class_impl<T, Bases>(m, name, doc, seq);
    }

    /** Bind the default constructor. */
    static void add_default_ctor(auto& cls) { cls.def(py::init<>()); }

    /** Bind constructor @a Ctor as a `py::init<…>`. @see def_init */
    template <std::meta::info Ctor>
    static void add_constructor(auto& cls) {
        def_init<Ctor>(cls, std::make_index_sequence<
                                std::meta::parameters_of(Ctor).size()>{});
    }

    /** Bind the synthesized aggregate field constructor. @see def_aggregate_init */
    template <class T>
    static void add_aggregate_constructor(auto& cls) {
        constexpr auto fields{welder::detail::aggregate_fields<T>()};
        def_aggregate_init<T>(cls, std::make_index_sequence<fields.size()>{});
    }

    /** Bind data member @a Mem as an attribute.

        pybind11 data members are already Python properties (data descriptors on the
        class), so a `[[=welder::doc]]` on the member rides along as the property's
        `__doc__` — and thus reaches `.pyi` stubs. A const member is read-only
        (`def_readonly`); a mutable one is read/write (`def_readwrite`). The doc,
        when present, is passed as the property docstring. There is deliberately no
        setter docstring: a Python `property` surfaces only the getter's `__doc__`,
        so one would be pure overhead. */
    template <std::meta::info Mem>
    static void add_field(auto& cls) {
        constexpr const char* name{
            std::define_static_string(std::meta::identifier_of(Mem))};
        constexpr const char* doc{welder::doc_of<Mem>()};
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
    template <std::meta::info Fn>
    static void add_method(auto& cls) {
        def_function<Fn>(std::define_static_string(std::meta::identifier_of(Fn)),
                         [&cls](auto&&... a) {
                             cls.def(std::forward<decltype(a)>(a)...);
                         });
    }

    /** Bind static member function @a Fn as a static method. */
    template <std::meta::info Fn>
    static void add_static_method(auto& cls) {
        def_function<Fn>(std::define_static_string(std::meta::identifier_of(Fn)),
                         [&cls](auto&&... a) {
                             cls.def_static(std::forward<decltype(a)>(a)...);
                         });
    }

    /** Bind member operator @a Fn under its Python dunder. */
    template <std::meta::info Fn>
    static void add_operator(auto& cls) {
        def_function<Fn>(operator_dunder_of(Fn), [&cls](auto&&... a) {
            cls.def(std::forward<decltype(a)>(a)...);
        });
    }

    // --- enum binding -------------------------------------------------------

    /** Create the `py::enum_<E>` handle (a non-null @a doc becomes its docstring). */
    template <class E>
    static auto make_enum(module_type& m, const char* name, const char* doc) {
        // A non-null doc is the enum docstring (a bare const char* extra); nullptr
        // is branched out rather than passed (pybind11 would strdup it).
        if (doc)
            return py::enum_<E>(m, name, doc);
        return py::enum_<E>(m, name);
    }

    /** Add enumerator @a Enum to the enum handle. */
    template <std::meta::info Enum>
    static void add_enumerator(auto& e) {
        e.value(std::define_static_string(std::meta::identifier_of(Enum)), [:Enum:]);
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
        properties; install_live_properties() applies them in one `__class__` swap
        at close. */
    static py::dict open_module(module_type&) { return py::dict{}; }

    /** Set the (sub)module docstring. */
    static void set_module_doc(module_type& m, const char* doc) { m.doc() = doc; }

    /** Bind free function @a Fn as a module-level function. */
    template <std::meta::info Fn>
    static void add_function(module_type& m) {
        def_function<Fn>(std::define_static_string(std::meta::identifier_of(Fn)),
                         [&m](auto&&... a) {
                             m.def(std::forward<decltype(a)>(a)...);
                         });
    }

    /** Bind namespace variable @a Var as a module attribute.

        A const/constexpr variable becomes a value snapshot; a mutable one becomes a
        live get/set property over the C++ global (accumulated in @a live). */
    template <std::meta::info Var>
    static void add_variable(module_type& m, py::dict& live) {
        constexpr const char* name{
            std::define_static_string(std::meta::identifier_of(Var))};
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
            install_live_properties(m, live);
    }
};

} // namespace detail

/** Reflect over @a T and register it on module @a m.

    A class type becomes a `py::class_`; an enum becomes a `py::enum_` (its
    enumerators resolve like data members, honoring the enum's policy/marks). See
    the driver in `<welder/backend.hpp>` for the full set of what is bound.

    @tparam T the type to bind.
    @param m    the module handle.
    @param name the Python name, or `nullptr` to default to @a T's identifier.
    @return the `class_`/`enum_` handle, so callers can chain further registrations.
*/
template <class T>
auto bind(py::module_& m, const char* name = nullptr) {
    if constexpr (std::is_enum_v<T>)
        return welder::detail::bind_enum<detail::backend, T>(m, name);
    else
        return welder::detail::bind_type<detail::backend, T>(m, name);
}

/** Reflect over a whole namespace and expose its welded members on module @a m.

    Classes bind via bind(), free functions, namespace variables, and nested
    namespaces as submodules. Usage:
    @code
    PYBIND11_MODULE(mymod, m) { welder::pybind11::bind_namespace<^^myns>(m); }
    @endcode

    @tparam Ns a reflection of the namespace.
    @param m the module handle.
*/
template <std::meta::info Ns>
void bind_namespace(py::module_& m) {
    welder::detail::bind_namespace_driver<detail::backend, Ns>(m);
}

/** A do-nothing module hook; the default for build_module()'s pre/post callbacks. */
inline constexpr auto noop = [](py::module_&) {};

/** Build a whole Python module out of top-level C++ namespace @a Ns.

    Runs @a pre, binds the namespace into @a m (adopting a namespace-level
    `[[=welder::doc]]` as the module docstring), then runs @a post. The hooks fold
    hand-written bindings in around welder's generated body. This fills an
    *existing* `py::module_`; pair it with an entry-point macro (pybind11's own, or
    welder's `WELDER_MODULE`) that emits the module's C entry symbol:
    @code
    PYBIND11_MODULE(shapes, m) { welder::pybind11::build_module<^^shapes>(m); }
    @endcode

    @tparam Ns   a reflection of the top-level namespace.
    @tparam Pre  the pre-hook callable type.
    @tparam Post the post-hook callable type.
    @param m    the module handle to fill.
    @param pre  invoked with @a m before binding (defaults to noop).
    @param post invoked with @a m after binding (defaults to noop).
*/
template <std::meta::info Ns, class Pre = decltype(noop), class Post = decltype(noop)>
void build_module(py::module_& m, Pre pre = noop, Post post = noop) {
    welder::detail::build_module_driver<detail::backend, Ns>(m, pre, post);
}

} // namespace welder::pybind11

/** @def WELDER_DETAIL_MODULE_ENTRY_pybind11
    pybind11's expansion of the backend-agnostic `WELDER_MODULE(ns, pybind11)`.

    Emits the `PyInit_<ns>` entry point, binds namespace `^^ns` into it, then runs
    the optional trailing `{ }` block as post-glue with the module handle named
    `module` in scope. The block is supplied as the body of a forward-declared,
    internally-linked glue function (the same technique `PYBIND11_MODULE` itself
    uses for its body), so `WELDER_MODULE(ns, pybind11) { … }` and
    `WELDER_MODULE(ns, pybind11) {}` both work. Defined at file scope (macros ignore
    namespaces); see `<welder/module.hpp>` for the `WELDER_MODULE` dispatch.
    @param ns the namespace / module name token.
*/
#define WELDER_DETAIL_MODULE_ENTRY_pybind11(ns)                                   \
    static void welder_glue_##ns##_pybind11(::pybind11::module_&);                \
    PYBIND11_MODULE(ns, welder_module_var_) {                                     \
        ::welder::pybind11::build_module<^^ns>(                                   \
            welder_module_var_, ::welder::pybind11::noop,                         \
            [](::pybind11::module_& welder_glue_m_) {                             \
                welder_glue_##ns##_pybind11(welder_glue_m_);                      \
            });                                                                   \
    }                                                                             \
    static void welder_glue_##ns##_pybind11(::pybind11::module_& module)
