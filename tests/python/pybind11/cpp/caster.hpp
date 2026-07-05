#pragma once
// Custom type_caster cases — mirrors tests/test_caster.py.
//
// A type welder never sees welded is still bindable *automatically* — no weld, no
// trust_bindable — when the user gives it a self-contained pybind11 type_caster
// (one that does NOT derive from type_caster_base, e.g. via PYBIND11_TYPE_CASTER).
// The specialization displaces pybind11's generic class-registration fallback, so
// _needs_registration is false, has_native_caster is true, and welder's bindability
// gate passes on its own. Because the caster converts to/from a native Python value
// (here a float), the member also stubs cleanly as that type.
//
// REQUIREMENT (standard pybind11 rule, not welder-specific): the type_caster<T>
// specialization must be visible *before* welder binds any type that uses T — else
// the make_caster<T> welder instantiates sees only the fallback and (correctly)
// rejects T. So the caster is declared here, ahead of the register_* call. gcc-16
// happens to defer the point of instantiation to end-of-TU, but relying on that is
// ill-formed-NDR; keep the caster ahead of the bind.
//
// The cases live in namespace `caster`, bound under a `caster` submodule via
// welder::pybind11::bind_namespace so the Python package mirrors this file. The
// namespace is split around the type_caster, which must sit in pybind11::detail.
//
// #included by bindings.cpp after the welder vocabulary + pybind11 backend, which
// bring pybind11::detail (and Python.h) into scope.

namespace caster {

// A weld-free, registration-free type whose Python form is a float.
struct Celsius {
    double value{0};
};

} // namespace caster

namespace pybind11 { namespace detail {
template <> struct type_caster<caster::Celsius> {
    PYBIND11_TYPE_CASTER(caster::Celsius, const_name("float"));
    bool load(handle src, bool) {
        PyObject* o{src.ptr()};
        if (!PyFloat_Check(o) && !PyLong_Check(o))
            return false;
        value.value = PyFloat_AsDouble(o);
        return !(value.value == -1.0 && PyErr_Occurred());
    }
    static handle cast(const caster::Celsius& c, return_value_policy, handle) {
        return PyFloat_FromDouble(c.value);
    }
};
}} // namespace pybind11::detail

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

inline void register_caster(pybind11::module_& m) {
    auto sub{m.def_submodule("caster")};
    welder::pybind11::bind_namespace<^^caster>(sub);
}
