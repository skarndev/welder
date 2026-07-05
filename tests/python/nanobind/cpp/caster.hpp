#pragma once
// Custom type_caster cases (nanobind) — mirrors tests/test_caster.py.
//
// A type welder never sees welded is still bindable *automatically* — no weld, no
// trust_bindable — when the user gives it a self-contained nanobind type_caster
// (one that does NOT derive from type_caster_base, e.g. via NB_TYPE_CASTER). The
// specialization displaces nanobind's generic class-registration fallback, so
// _needs_registration is false, has_native_caster is true, and welder's bindability
// gate passes on its own. Because the caster converts to/from a native Python value
// (here a float), the member also reads cleanly as that type.
//
// This is the nanobind counterpart of tests/pybind11/cpp/caster.hpp: same cases and
// same submodule (`caster`), against nanobind's caster API (NB_TYPE_CASTER +
// from_python/from_cpp, sitting in nanobind::detail).
//
// #included by bindings.cpp after the welder vocabulary + the nanobind backend,
// which bring nanobind::detail (and Python.h) into scope.

namespace caster {

// A weld-free, registration-free type whose Python form is a float.
struct Celsius {
    double value{0};
};

} // namespace caster

namespace nanobind { namespace detail {
template <> struct type_caster<caster::Celsius> {
    bool from_python(handle src, uint8_t, cleanup_list*) noexcept {
        PyObject* o{src.ptr()};
        if (!PyFloat_Check(o) && !PyLong_Check(o))
            return false;
        double d{PyFloat_AsDouble(o)};
        if (d == -1.0 && PyErr_Occurred())
            return false;
        value.value = d;
        return true;
    }
    static handle from_cpp(const caster::Celsius& c, rv_policy, cleanup_list*) noexcept {
        return PyFloat_FromDouble(c.value);
    }
    NB_TYPE_CASTER(caster::Celsius, const_name("float"))
};
}} // namespace nanobind::detail

namespace caster {

struct
[[=welder::weld(welder::lang::py)]]
Sample {
    // a data member of the custom-caster type -> bound with no weld/trust
    Celsius temperature;

    int sensor{0};

    // and in a method signature (parameter + return) -> also bound
    Celsius warmer(Celsius by) const { return Celsius{temperature.value + by.value}; }
};

} // namespace caster

inline void register_caster(nanobind::module_& m) {
    auto sub{m.def_submodule("caster")};
    welder::nanobind::bind_namespace<^^caster>(sub);
}
