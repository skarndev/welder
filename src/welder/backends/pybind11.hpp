#pragma once
//
// welder pybind11 backend (header-only).
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
#include <type_traits>
#include <utility>

#include <welder/reflect.hpp> // <meta> + resolution (welded_for, member_bound)

#include <pybind11/pybind11.h>

namespace welder::pybind11 {

// Inside `welder::pybind11`, the unqualified name `pybind11` resolves to *this*
// namespace, not the library. Alias the real one once — the way pybind11's own
// docs spell it — and use `py::` for every library reference below.
namespace py = ::pybind11;

namespace detail {

// A function's parameter *types*, as a static array of reflections (usable as a
// non-type template argument so it can be spliced back into a pack).
template <std::meta::info Fn>
consteval auto param_types() {
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    std::array<std::meta::info, n> types{};
    std::size_t i{0};
    for (auto p : std::meta::parameters_of(Fn))
        types[i++] = std::meta::type_of(p);
    return types;
}

// Register py::init<P0, P1, ...>() from reflected parameter types.
template <class T, auto Params, std::size_t... I>
void def_init(py::class_<T>& cls, std::index_sequence<I...>) {
    cls.def(py::init<typename [:Params[I]:]...>());
}

// A non-default, non-copy/move public constructor we should expose. The default
// constructor is handled separately (it may be implicit, hence not a member).
consteval bool is_bindable_constructor(std::meta::info c) {
    return std::meta::is_constructor(c) && std::meta::is_public(c) &&
           !std::meta::is_deleted(c) && !std::meta::is_copy_constructor(c) &&
           !std::meta::is_move_constructor(c) &&
           !std::meta::parameters_of(c).empty();
}

// A member function we should expose as a method, honoring the same
// exclude/include/policy resolution as data members. Special members,
// destructors and operators are skipped.
consteval bool is_bindable_method(std::meta::info f, lang L, policy_kind pol) {
    return std::meta::is_function(f) && !std::meta::is_constructor(f) &&
           !std::meta::is_special_member_function(f) &&
           !std::meta::is_destructor(f) && !std::meta::is_operator_function(f) &&
           std::meta::is_public(f) && !std::meta::is_deleted(f) &&
           member_bound(f, L, pol);
}

// --- inheritance ------------------------------------------------------------
//
// `weld` marks a type as an independently-registered, module-discoverable
// entity, NOT as an inheritance directive. The most-derived type's `weld` drives
// which languages we bind; a base need not be welded to contribute its members.
// From that, two kinds of public base fall out for a binding:
//   * a welded base -> it is (or will be) registered as its own Python class, so
//     we link to it via "native" pybind11 inheritance: it is passed as a base of
//     class_<T, Base...> and its members reach Python through the usual class
//     hierarchy (issubclass, polymorphism). Bind it separately, before T.
//   * a non-welded base -> a plain C++ "mixin" with no standalone Python type, so
//     its eligible members/methods are flattened directly onto the derived
//     binding (see bind_members).
// In both cases a base's members honor that base's own policy and marks.

// The native pybind11 bases of Type for L: its nearest welded ancestors, found
// by looking *past* non-welded bases (whose members are flattened instead). So a
// welded base reachable only through a non-welded one is still linked. Reflexes
// like a virtual diamond can reach the same welded base by several paths, so the
// list is deduplicated.
consteval void collect_native_bases(std::meta::info type, lang L,
                                    std::vector<std::meta::info>& out) {
    for (auto base : welder::public_bases(type)) {
        if (welder::welded_for(base, L)) {
            bool seen{false};
            for (auto e : out)
                if (e == base) {
                    seen = true;
                    break;
                }
            if (!seen)
                out.push_back(base);
        } else {
            collect_native_bases(base, L, out); // descend through the mixin
        }
    }
}

// The same set as a static array of type reflections usable as a non-type
// template argument (spliced into class_<T, Bases...>).
template <std::meta::info Type, lang L>
consteval auto native_base_types() {
    constexpr std::size_t n{[] {
        std::vector<std::meta::info> v;
        collect_native_bases(Type, L, v);
        return v.size();
    }()};
    std::array<std::meta::info, n> types{};
    // Guard the fill: std::array<T, 0>::operator[] is not consteval, so it must
    // not be instantiated when a type has no native bases (the common case).
    if constexpr (n != 0) {
        std::vector<std::meta::info> v;
        collect_native_bases(Type, L, v);
        std::size_t i{0};
        for (auto base : v)
            types[i++] = base;
    }
    return types;
}

// Construct py::class_<T, NativeBases...> from a reflected base-type array.
template <class T, auto Bases, std::size_t... I>
auto make_class(py::module_& m, const char* name, std::index_sequence<I...>) {
    return py::class_<T, typename [:Bases[I]:]...>(m, name);
}

// Bind the eligible public data members and methods of Src onto `cls` (a
// class_<T, ...> for the type being bound, where T derives from Src). Then
// recurse into Src's non-welded public bases so a chain of plain mixins folds in
// too. Constructors are never flattened: the derived type provides its own.
//
// Non-welded bases are flattened *before* Src's own members so that, on a name
// clash, the member declared closer to the derived type wins in Python.
template <std::meta::info Src, lang L, class Cls>
void bind_members(Cls& cls) {
    constexpr auto ctx{std::meta::access_context::unchecked()};

    template for (constexpr auto base :
                  std::define_static_array(welder::public_bases(Src))) {
        if constexpr (!welder::welded_for(base, L))
            bind_members<base, L>(cls);
    }

    constexpr policy_kind pol{policy_of(Src)};

    template for (constexpr auto mem : std::define_static_array(
                      std::meta::nonstatic_data_members_of(Src, ctx))) {
        if constexpr (std::meta::is_public(mem) && member_bound(mem, L, pol)) {
            constexpr const char* mname{
                std::define_static_string(std::meta::identifier_of(mem))};
            cls.def_readwrite(mname, &[:mem:]);
        }
    }

    template for (constexpr auto fn :
                  std::define_static_array(std::meta::members_of(Src, ctx))) {
        if constexpr (is_bindable_method(fn, L, pol)) {
            constexpr const char* fname{
                std::define_static_string(std::meta::identifier_of(fn))};
            if constexpr (std::meta::is_static_member(fn))
                cls.def_static(fname, &[:fn:]);
            else
                cls.def(fname, &[:fn:]);
        }
    }
}

// --- namespace introspection ------------------------------------------------
//
// bind_namespace<^^ns> scans a namespace and exposes its members. `weld` is the
// discovery gate: a class type, free function or namespace-scope variable is a
// *candidate* iff welded for L. Whether a candidate actually binds is then
// resolved exactly like a struct member — by the namespace's `policy` (default
// automatic) and the member's exclude/include marks (member_bound). So a welded
// entity can be suppressed per-language with mark::exclude, and an opt_in
// namespace binds only its welded-and-included members.
//
//   class type          -> bind<T>
//   free function        -> module function (overloads included)
//   namespace-scope var  -> module attribute: a value snapshot if const/constexpr,
//                           else a live get/set property over the C++ global
//   nested namespace     -> submodule (pruned by mark::exclude; recursed when it
//                           holds bound content, under its own policy)

// The member kinds welder can expose. is_class_type throws on a non-type
// reflection, so it is reached only after is_type; the other predicates are
// total and safe on any reflection.
consteval bool is_bindable_kind(std::meta::info mem) {
    return (std::meta::is_type(mem) && std::meta::is_class_type(mem)) ||
           std::meta::is_function(mem) || std::meta::is_variable(mem);
}

// A leaf entity binds iff it is a welded candidate that also resolves as bound
// under namespace policy `pol` and its own marks.
consteval bool entity_bound(std::meta::info mem, lang L, policy_kind pol) {
    return is_bindable_kind(mem) && welded_for(mem, L) && member_bound(mem, L, pol);
}

// Whether `ns` holds anything that would bind, directly or nested — i.e. whether
// exposing it would yield a non-empty (sub)module. Each namespace contributes
// under its own policy; a nested namespace is recursed by the same rule as the
// dispatch (member_bound under ns's policy: automatic unless excluded, opt_in
// only if included).
consteval bool namespace_has_bound(std::meta::info ns, lang L) {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    const policy_kind pol{policy_of(ns)};
    for (auto mem : std::meta::members_of(ns, ctx)) {
        if (std::meta::is_namespace(mem)) {
            if (member_bound(mem, L, pol) && namespace_has_bound(mem, L))
                return true;
        } else if (entity_bound(mem, L, pol)) {
            return true;
        }
    }
    return false;
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

} // namespace detail

// Forward declarations. bind_namespace and its per-member dispatch are mutually
// recursive (a nested namespace becomes a submodule bound the same way), and the
// dispatch defers to bind<T> for class members, defined further below.
template <class T>
auto bind(py::module_& m, const char* name = nullptr);
template <std::meta::info Ns>
void bind_namespace(py::module_& m);

namespace detail {

// Expose a single namespace member onto module `m`, resolved under namespace
// policy Pol. Mutable variables accumulate into `live` for one __class__ swap by
// the caller. Wrapped in a template (over the reflection) so the discarded
// if constexpr branches — and their kind-specific splices — are never
// instantiated for the wrong kind.
template <std::meta::info M, policy_kind Pol>
void bind_namespace_member(py::module_& m, py::dict& live) {
    constexpr lang L{lang::py};
    if constexpr (std::meta::is_type(M) && std::meta::is_class_type(M)) {
        if constexpr (welded_for(M, L) && member_bound(M, L, Pol))
            bind<typename [:M:]>(m);
    } else if constexpr (std::meta::is_function(M)) {
        if constexpr (welded_for(M, L) && member_bound(M, L, Pol))
            m.def(std::define_static_string(std::meta::identifier_of(M)), &[:M:]);
    } else if constexpr (std::meta::is_variable(M)) {
        if constexpr (welded_for(M, L) && member_bound(M, L, Pol)) {
            constexpr const char* vname{
                std::define_static_string(std::meta::identifier_of(M))};
            if constexpr (std::meta::is_const_type(std::meta::type_of(M))) {
                m.attr(vname) = [:M:]; // immutable: a value snapshot at bind time
            } else {
                // Mutable: a live property over the C++ global. The descriptors
                // take a leading `self` (the module) and ignore it.
                auto property{py::module_::import("builtins").attr("property")};
                live[vname] = property(
                    py::cpp_function([](py::object) { return [:M:]; }),
                    py::cpp_function(
                        [](py::object, typename [:std::meta::type_of(M):] v) {
                            [:M:] = v;
                        }));
            }
        }
    } else if constexpr (std::meta::is_namespace(M)) {
        // A nested namespace is resolved like a leaf member under the parent
        // policy Pol (but is never welded): automatic recurses unless excluded,
        // opt_in recurses only if included. It becomes a submodule when it holds
        // bound content (which is then resolved under its *own* policy).
        if constexpr (member_bound(M, L, Pol) && namespace_has_bound(M, L)) {
            auto sub{m.def_submodule(
                std::define_static_string(std::meta::identifier_of(M)))};
            bind_namespace<M>(sub);
        }
    }
}

} // namespace detail

// Reflect over T and register, on a py::class_<T, NativeBases...>:
//   * native inheritance from T's nearest welded ancestors (each passed as a
//     pybind11 base; bind those bases separately, before T). Welded bases reached
//     only through non-welded ones are still linked.
//   * constructors (the default ctor if T is default-constructible, plus each
//     public non-copy/move constructor);
//   * public data members that resolve as bound for Python;
//   * public member functions (methods / static methods) that resolve as bound;
//   * the eligible members/methods of every non-welded public base, flattened in
//     (a plain C++ base behaves like a mixin folded into each derived binding).
//
// Member-level exclude/include/policy marks apply to both data members and
// methods. Custom type converters are expected to be registered separately
// through pybind11's own mechanisms (design TODO).
//
// `name` defaults to the identifier of T. Returns the class_ (its exact type
// carries the native bases, hence the deduced return) so callers can chain
// additional pybind11 registrations.
template <class T>
auto bind(py::module_& m, const char* name) {
    constexpr lang L{lang::py};
    static_assert(welded_for(^^T, L),
                  "welder::pybind11::bind<T>: T is not welded for Python; annotate "
                  "it with [[=welder::weld(welder::lang::py)]]");
    constexpr auto ctx{std::meta::access_context::unchecked()};

    const char* cls_name{
        name ? name : std::define_static_string(std::meta::identifier_of(^^T))};

    // --- native (welded) bases ----------------------------------------------
    // class_<T, Base...> wires up the Python class hierarchy for bases that are
    // themselves welded; the user binds those bases separately.
    constexpr auto bases{detail::native_base_types<^^T, L>()};
    auto cls{detail::make_class<T, bases>(
        m, cls_name, std::make_index_sequence<bases.size()>{})};

    // --- constructors --------------------------------------------------------
    if constexpr (std::is_default_constructible_v<T>)
        cls.def(py::init<>());
    template for (constexpr auto ctor :
                  std::define_static_array(std::meta::members_of(^^T, ctx))) {
        if constexpr (detail::is_bindable_constructor(ctor)) {
            constexpr auto params{detail::param_types<ctor>()};
            detail::def_init<T, params>(cls,
                                        std::make_index_sequence<params.size()>{});
        }
    }

    // --- data members + methods (T's own, plus flattened non-welded bases) ---
    // Members are queried with an unchecked context (so welder *sees* every
    // member) but only public ones bind: private/protected members are not part
    // of the public API exposed to Python.
    detail::bind_members<^^T, L>(cls);

    return cls;
}

// Reflect over a whole namespace and expose its members on module `m`. `weld`
// makes an entity a candidate; the namespace `policy` (default automatic) and
// per-member exclude/include marks then resolve what binds, mirroring struct
// member resolution. Exposed:
//   * each welded class type, via bind<T> (so its own bases/members resolve as
//     usual; declare a welded base before its derived types — C++ already
//     requires this within a namespace);
//   * each welded free function, as a module-level function (overloads bind as
//     one overloaded callable);
//   * each welded namespace-scope variable, as a module attribute: a value
//     snapshot if it is const/constexpr, otherwise a live get/set property over
//     the C++ global;
//   * each nested namespace holding bound content, as a submodule.
//
// Members are visited in declaration order. Usage:
//   PYBIND11_MODULE(mymod, m) { welder::pybind11::bind_namespace<^^myns>(m); }
template <std::meta::info Ns>
void bind_namespace(py::module_& m) {
    static_assert(std::meta::is_namespace(Ns),
                  "welder::pybind11::bind_namespace<Ns>: Ns must reflect a namespace");
    constexpr auto ctx{std::meta::access_context::unchecked()};
    constexpr policy_kind pol{policy_of(Ns)};
    // Collects live (mutable-variable) properties; installed in one __class__
    // swap after all members are visited (only if any were produced).
    py::dict live;
    template for (constexpr auto mem :
                  std::define_static_array(std::meta::members_of(Ns, ctx)))
        detail::bind_namespace_member<mem, pol>(m, live);
    if (live.size() != 0)
        detail::install_live_properties(m, live);
}

} // namespace welder::pybind11
