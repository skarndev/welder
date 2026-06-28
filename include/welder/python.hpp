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
#include <type_traits>

#include <welder/reflect.hpp> // <meta> + resolution (welded_for, member_bound)

#include <pybind11/pybind11.h>

namespace welder::py {

// Reflect over T, ask the core which members participate for lang::py, and
// register them on a pybind11::class_<T>. Custom type converters are expected to
// be registered separately through pybind11's own mechanisms (design TODO).
//
// `name` defaults to the identifier of T. Returns the class_ so callers can
// chain additional pybind11 registrations (methods, properties, ...).
template <class T>
pybind11::class_<T> bind(pybind11::module_& m, const char* name = nullptr) {
    constexpr lang L = lang::py;
    static_assert(welded_for(^^T, L),
                  "welder::py::bind<T>: T is not welded for Python; annotate it "
                  "with [[=welder::weld(welder::lang::py)]]");
    constexpr policy_kind pol = policy_of(^^T);

    const char* cls_name =
        name ? name : std::define_static_string(std::meta::identifier_of(^^T));

    pybind11::class_<T> cls(m, cls_name);

    if constexpr (std::is_default_constructible_v<T>)
        cls.def(pybind11::init<>());

    template for (constexpr auto mem : std::define_static_array(
                      std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked()))) {
        if constexpr (member_bound(mem, L, pol)) {
            constexpr const char* mname =
                std::define_static_string(std::meta::identifier_of(mem));
            cls.def_readwrite(mname, &[:mem:]);
        }
    }

    return cls;
}

} // namespace welder::py
