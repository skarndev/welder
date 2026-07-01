#pragma once
//
// welder pybind11 backend (header-only).
//
// This is a *thin* backend: it implements welder's backend contract
// (<welder/backend.hpp>) for pybind11 and hands the traversal/resolution off to
// welder's generic driver. All the language-agnostic work — deciding which
// members bind, gating bindability, folding docstrings, walking bases and
// namespaces — lives in the core; only the pybind11 emission primitives are here.
// A future nanobind / lua backend mirrors this file against its own framework.
//
// Requires the welder vocabulary to be available first, via either:
//   * `import welder;`                  (module form), or
//   * `#include <welder/welder.hpp>`    (header-only form)
//
// Then:
//   #include <pybind11/pybind11.h>
//   #include <welder/backends/pybind11.hpp>
//   PYBIND11_MODULE(mymod, m) { welder::pybind11::bind<MyType>(m); }
//
#include <array>
#include <cstddef>
#include <meta>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/backend.hpp>    // the backend contract + generic driver
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

// Whether pybind11 handles T only via *runtime class registration* — i.e. its
// caster is the generic type_caster_base fallback rather than a specialized one.
// True for program-defined classes and unregistered enums; false for scalars,
// strings, the STL containers (with <pybind11/stl.h>), and — crucially — any type
// the user gave a bespoke pybind11 type_caster (that specialization displaces the
// fallback, so welder automatically trusts it). This is the one bindability fact
// welder's core cannot know on its own; it drives caster_oracle below.
template <class T>
inline constexpr bool needs_registration =
    std::is_base_of_v<py::detail::type_caster_base<py::detail::intrinsic_t<T>>,
                      py::detail::make_caster<T>>;

// The Python special-method ("dunder") name for a member operator, or nullptr if
// welder does not expose that operator. Unary vs binary is told apart by arity (a
// member operator takes 0 parameters when unary, 1 when binary), disambiguating
// the operators that have both forms (+, -). In-place compound assignments
// (operator+=, ...) are intentionally not mapped: Python already falls back to the
// binary form (a += b -> a = a + b via __add__) with correct value semantics, and
// binding __iadd__ faithfully would need a reference return policy. Likewise <=>,
// &&, ||, ++, -- and = have no clean reflection-driven Python mapping yet.
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

// Register the function/method reflected by Fn onto a pybind11 target. `def_into`
// adapts the target — `cls.def`, `cls.def_static`, or `m.def`. The folded
// docstring is passed when non-empty, and `py::arg(name)...` when every parameter
// is named (so Python callers see real keyword arguments, not arg0/arg1).
template <std::meta::info Fn, class Def, std::size_t... I>
void def_function(const char* name, Def def_into, std::index_sequence<I...>) {
    static constexpr auto names{welder::detail::param_names<Fn>()};
    const std::string doc{welder::function_docstring<Fn>()};
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

template <std::meta::info Fn, class Def>
void def_function(const char* name, Def def_into) {
    def_function<Fn>(name, def_into,
                     std::make_index_sequence<
                         std::meta::parameters_of(Fn).size()>{});
}

// Register py::init<P0, P1, ...>() for constructor Ctor, naming the parameters
// (py::arg) when all are named, otherwise positional.
template <std::meta::info Ctor, std::size_t... I>
void def_init(auto& cls, std::index_sequence<I...>) {
    static constexpr auto params{welder::detail::param_types<Ctor>()};
    static constexpr auto names{welder::detail::param_names<Ctor>()};
    if constexpr (welder::detail::all_params_named<Ctor>())
        cls.def(py::init<typename [:params[I]:]...>(), py::arg(names[I])...);
    else
        cls.def(py::init<typename [:params[I]:]...>());
}

// Synthesize py::init([](F0 f0, ...) { return T{f0, ...}; }, py::arg("f0"), ...)
// for a baseless aggregate, so Python can build it from field values (T(f0, f1))
// rather than only default-construct + assign.
template <class T, std::size_t... I>
void def_aggregate_init(auto& cls, std::index_sequence<I...>) {
    static constexpr auto fields{welder::detail::aggregate_fields<T>()};
    cls.def(py::init([](typename [:std::meta::type_of(fields[I]):]... args) {
                return T{std::move(args)...};
            }),
            py::arg(std::define_static_string(
                std::meta::identifier_of(fields[I])))...);
}

// Reassign `m`'s Python class to a fresh ModuleType subclass carrying `props`
// (name -> property), giving those names live get/set semantics. Python modules
// don't support properties directly, but a module's __class__ may be swapped for
// a ModuleType subclass. Used only when a namespace exposes a mutable variable.
inline void install_live_properties(py::module_& m, py::dict props) {
    auto builtins{py::module_::import("builtins")};
    auto types{py::module_::import("types")};
    auto subclass{builtins.attr("type")(
        py::str("welder_live_module"),
        py::make_tuple(types.attr("ModuleType")), props)};
    m.attr("__class__") = subclass;
}

// Construct py::class_<T, NativeBases...> from a reflected base-type array. A
// non-null `doc` becomes the class docstring (pybind11 treats a bare const char*
// extra as the doc); nullptr is branched out rather than passed, since pybind11
// would strdup it.
template <class T, auto Bases, std::size_t... I>
auto make_class_impl(py::module_& m, const char* name, const char* doc,
                     std::index_sequence<I...>) {
    if (doc)
        return py::class_<T, typename [:Bases[I]:]...>(m, name, doc);
    return py::class_<T, typename [:Bases[I]:]...>(m, name);
}

// The pybind11 backend: a stateless policy type satisfying welder::backend. Its
// static members are the pybind11 emission primitives welder's driver calls; the
// driver supplies all the reflection-derived decisions. See <welder/backend.hpp>
// for the contract each member fulfills.
struct backend {
    static constexpr lang language{lang::py};
    using module_type = py::module_;

    // caster_oracle: T is convertible without welder registering a class for it
    // iff pybind11 does *not* fall back to runtime class registration.
    template <class T>
    static constexpr bool has_native_caster = !needs_registration<T>;

    // operator -> Python dunder (nullptr = not exposed).
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return operator_dunder_of(op_fn);
    }

    // --- class binding ------------------------------------------------------
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* doc,
                           std::index_sequence<I...> seq) {
        return make_class_impl<T, Bases>(m, name, doc, seq);
    }

    static void add_default_ctor(auto& cls) { cls.def(py::init<>()); }

    template <std::meta::info Ctor>
    static void add_constructor(auto& cls) {
        def_init<Ctor>(cls, std::make_index_sequence<
                                std::meta::parameters_of(Ctor).size()>{});
    }

    template <class T>
    static void add_aggregate_constructor(auto& cls) {
        constexpr auto fields{welder::detail::aggregate_fields<T>()};
        def_aggregate_init<T>(cls, std::make_index_sequence<fields.size()>{});
    }

    template <std::meta::info Mem>
    static void add_field(auto& cls) {
        cls.def_readwrite(
            std::define_static_string(std::meta::identifier_of(Mem)), &[:Mem:]);
    }

    template <std::meta::info Fn>
    static void add_method(auto& cls) {
        def_function<Fn>(std::define_static_string(std::meta::identifier_of(Fn)),
                         [&cls](auto&&... a) {
                             cls.def(std::forward<decltype(a)>(a)...);
                         });
    }

    template <std::meta::info Fn>
    static void add_static_method(auto& cls) {
        def_function<Fn>(std::define_static_string(std::meta::identifier_of(Fn)),
                         [&cls](auto&&... a) {
                             cls.def_static(std::forward<decltype(a)>(a)...);
                         });
    }

    template <std::meta::info Fn>
    static void add_operator(auto& cls) {
        def_function<Fn>(operator_dunder_of(Fn), [&cls](auto&&... a) {
            cls.def(std::forward<decltype(a)>(a)...);
        });
    }

    // --- namespace / module binding -----------------------------------------
    // The session is a dict accumulating live (mutable-variable) properties;
    // install_live_properties applies them in one __class__ swap at close.
    static py::dict open_module(module_type&) { return py::dict{}; }

    static void set_module_doc(module_type& m, const char* doc) { m.doc() = doc; }

    template <std::meta::info Fn>
    static void add_function(module_type& m) {
        def_function<Fn>(std::define_static_string(std::meta::identifier_of(Fn)),
                         [&m](auto&&... a) {
                             m.def(std::forward<decltype(a)>(a)...);
                         });
    }

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

    static module_type add_submodule(module_type& m, const char* name) {
        return m.def_submodule(name);
    }

    static void close_module(module_type& m, py::dict& live) {
        if (live.size() != 0)
            install_live_properties(m, live);
    }
};

} // namespace detail

// Reflect over T and register it as a py::class_ on module `m` (see the driver in
// <welder/backend.hpp> for the full set of what is bound). `name` defaults to the
// identifier of T. Returns the class_ so callers can chain further pybind11
// registrations.
template <class T>
auto bind(py::module_& m, const char* name = nullptr) {
    return welder::detail::bind_type<detail::backend, T>(m, name);
}

// Reflect over a whole namespace and expose its welded members on module `m`
// (classes via bind<T>, free functions, namespace variables, nested namespaces
// as submodules). Usage:
//   PYBIND11_MODULE(mymod, m) { welder::pybind11::bind_namespace<^^myns>(m); }
template <std::meta::info Ns>
void bind_namespace(py::module_& m) {
    welder::detail::bind_namespace_driver<detail::backend, Ns>(m);
}

// A do-nothing module hook; the default for build_module's pre/post callbacks.
inline constexpr auto noop = [](py::module_&) {};

// Build a whole Python module out of top-level C++ namespace `Ns`: run `pre`, bind
// the namespace into `m` (adopting a namespace-level [[=welder::doc]] as the
// module docstring), then run `post`. The hooks fold hand-written bindings in
// around welder's generated body. This fills an *existing* py::module_; pair it
// with an entry-point macro (pybind11's own, or welder's WELDER_MODULE) that emits
// the module's C entry symbol:
//
//   PYBIND11_MODULE(shapes, m) { welder::pybind11::build_module<^^shapes>(m); }
template <std::meta::info Ns, class Pre = decltype(noop), class Post = decltype(noop)>
void build_module(py::module_& m, Pre pre = noop, Post post = noop) {
    welder::detail::build_module_driver<detail::backend, Ns>(m, pre, post);
}

} // namespace welder::pybind11

// pybind11's expansion of the backend-agnostic WELDER_MODULE(ns, pybind11): emit
// the PyInit_<ns> entry point, bind namespace ^^ns into it, then run the optional
// trailing { } block as post-glue with the module handle named `module` in scope.
// The block is supplied as the body of a forward-declared, internally-linked glue
// function (the same technique PYBIND11_MODULE itself uses for its body), so
// `WELDER_MODULE(ns, pybind11) { ... }` and `WELDER_MODULE(ns, pybind11) {}` both
// work. Defined at file scope (macros ignore namespaces); see <welder/module.hpp>
// for the WELDER_MODULE dispatch.
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
