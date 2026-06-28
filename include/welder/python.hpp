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

} // namespace detail

// Reflect over T and register, on a pybind11::class_<T>:
//   * constructors (the default ctor if T is default-constructible, plus each
//     public non-copy/move constructor);
//   * public data members that resolve as bound for Python;
//   * public member functions (methods / static methods) that resolve as bound.
//
// Member-level exclude/include/policy marks apply to both data members and
// methods. Custom type converters are expected to be registered separately
// through pybind11's own mechanisms (design TODO).
//
// `name` defaults to the identifier of T. Returns the class_ so callers can
// chain additional pybind11 registrations.
template <class T>
pybind11::class_<T> bind(pybind11::module_& m, const char* name = nullptr) {
    constexpr lang L = lang::py;
    static_assert(welded_for(^^T, L),
                  "welder::py::bind<T>: T is not welded for Python; annotate it "
                  "with [[=welder::weld(welder::lang::py)]]");
    constexpr policy_kind pol = policy_of(^^T);
    constexpr auto ctx = std::meta::access_context::unchecked();

    const char* cls_name =
        name ? name : std::define_static_string(std::meta::identifier_of(^^T));
    pybind11::class_<T> cls(m, cls_name);

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

    // --- data members --------------------------------------------------------
    template for (constexpr auto mem : std::define_static_array(
                      std::meta::nonstatic_data_members_of(^^T, ctx))) {
        if constexpr (member_bound(mem, L, pol)) {
            constexpr const char* mname =
                std::define_static_string(std::meta::identifier_of(mem));
            cls.def_readwrite(mname, &[:mem:]);
        }
    }

    // --- methods -------------------------------------------------------------
    template for (constexpr auto fn :
                  std::define_static_array(std::meta::members_of(^^T, ctx))) {
        if constexpr (detail::is_bindable_method(fn, L, pol)) {
            constexpr const char* fname =
                std::define_static_string(std::meta::identifier_of(fn));
            if constexpr (std::meta::is_static_member(fn))
                cls.def_static(fname, &[:fn:]);
            else
                cls.def(fname, &[:fn:]);
        }
    }

    return cls;
}

} // namespace welder::py
