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
//   #include <welder/python.hpp>
//   PYBIND11_MODULE(mymod, m) { welder::py::bind<MyType>(m); }
//
#include <array>
#include <type_traits>
#include <utility>

#include <welder/reflect.hpp> // <meta> + resolution (welded_for, member_bound)

#include <pybind11/pybind11.h>

namespace welder::py {

namespace detail {

// A function's parameter *types*, as a static array of reflections (usable as a
// non-type template argument so it can be spliced back into a pack).
template <std::meta::info Fn>
consteval auto param_types() {
    constexpr std::size_t n = std::meta::parameters_of(Fn).size();
    std::array<std::meta::info, n> types{};
    std::size_t i = 0;
    for (auto p : std::meta::parameters_of(Fn))
        types[i++] = std::meta::type_of(p);
    return types;
}

// Register pybind11::init<P0, P1, ...>() from reflected parameter types.
template <class T, auto Params, std::size_t... I>
void def_init(pybind11::class_<T>& cls, std::index_sequence<I...>) {
    cls.def(pybind11::init<typename [:Params[I]:]...>());
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
            bool seen = false;
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
    constexpr std::size_t n = [] {
        std::vector<std::meta::info> v;
        collect_native_bases(Type, L, v);
        return v.size();
    }();
    std::array<std::meta::info, n> types{};
    // Guard the fill: std::array<T, 0>::operator[] is not consteval, so it must
    // not be instantiated when a type has no native bases (the common case).
    if constexpr (n != 0) {
        std::vector<std::meta::info> v;
        collect_native_bases(Type, L, v);
        std::size_t i = 0;
        for (auto base : v)
            types[i++] = base;
    }
    return types;
}

// Construct pybind11::class_<T, NativeBases...> from a reflected base-type array.
template <class T, auto Bases, std::size_t... I>
auto make_class(pybind11::module_& m, const char* name,
                std::index_sequence<I...>) {
    return pybind11::class_<T, typename [:Bases[I]:]...>(m, name);
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
    constexpr auto ctx = std::meta::access_context::unchecked();

    template for (constexpr auto base :
                  std::define_static_array(welder::public_bases(Src))) {
        if constexpr (!welder::welded_for(base, L))
            bind_members<base, L>(cls);
    }

    constexpr policy_kind pol = policy_of(Src);

    template for (constexpr auto mem : std::define_static_array(
                      std::meta::nonstatic_data_members_of(Src, ctx))) {
        if constexpr (std::meta::is_public(mem) && member_bound(mem, L, pol)) {
            constexpr const char* mname =
                std::define_static_string(std::meta::identifier_of(mem));
            cls.def_readwrite(mname, &[:mem:]);
        }
    }

    template for (constexpr auto fn :
                  std::define_static_array(std::meta::members_of(Src, ctx))) {
        if constexpr (is_bindable_method(fn, L, pol)) {
            constexpr const char* fname =
                std::define_static_string(std::meta::identifier_of(fn));
            if constexpr (std::meta::is_static_member(fn))
                cls.def_static(fname, &[:fn:]);
            else
                cls.def(fname, &[:fn:]);
        }
    }
}

} // namespace detail

// Reflect over T and register, on a pybind11::class_<T, NativeBases...>:
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
auto bind(pybind11::module_& m, const char* name = nullptr) {
    constexpr lang L = lang::py;
    static_assert(welded_for(^^T, L),
                  "welder::py::bind<T>: T is not welded for Python; annotate it "
                  "with [[=welder::weld(welder::lang::py)]]");
    constexpr auto ctx = std::meta::access_context::unchecked();

    const char* cls_name =
        name ? name : std::define_static_string(std::meta::identifier_of(^^T));

    // --- native (welded) bases ----------------------------------------------
    // class_<T, Base...> wires up the Python class hierarchy for bases that are
    // themselves welded; the user binds those bases separately.
    constexpr auto bases = detail::native_base_types<^^T, L>();
    auto cls = detail::make_class<T, bases>(
        m, cls_name, std::make_index_sequence<bases.size()>{});

    // --- constructors --------------------------------------------------------
    if constexpr (std::is_default_constructible_v<T>)
        cls.def(pybind11::init<>());
    template for (constexpr auto ctor :
                  std::define_static_array(std::meta::members_of(^^T, ctx))) {
        if constexpr (detail::is_bindable_constructor(ctor)) {
            constexpr auto params = detail::param_types<ctor>();
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

} // namespace welder::py
