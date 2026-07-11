#pragma once
// Virtual-method overriding via a welder trampoline — mirrors tests/test_trampoline.py.
//
// The virtual class is backend-neutral; the trampoline uses welder's neutral
// WELDER_PY_TRAMPOLINE / WELDER_PY_OVERRIDE macros, so the SAME source binds under
// either Python rod. The binding TU must include the active backend's
// <welder/rods/python/<backend>/trampoline.hpp> *before* this header (for the macros
// and welder::rods::python::{trampoline_for,bind_flat}). The Lua backends do not
// include this header — trampolines are a Python-family concept.
//
// The cases live in namespace `overridable`, bound under an `overridable` submodule
// via WELDER_TEST_WELDER::weld_namespace so the Python package mirrors this file.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.

namespace overridable {

// A polymorphic base a Python subclass can override.
struct
[[=welder::weld(welder::lang::py)]]
Animal {
    virtual ~Animal() = default;

    [[=welder::doc("The sound this animal makes.")]]
    virtual std::string speak() const { return "..."; }

    virtual int legs() const { return 4; }

    // A virtual deliberately bound *flat*: it stays a plain, callable method but is
    // not routed through the trampoline, so it needs no override and drops out of the
    // slot count and coverage check. C++ never dispatches it back into Python.
    [[=welder::rods::python::bind_flat]]
    virtual std::string kingdom() const { return "Animalia"; }

    // A non-virtual method that calls the virtuals polymorphically: observing its
    // result from Python proves a C++ call dispatches into the Python override.
    std::string describe() const {
        return speak() + " on " + std::to_string(legs()) + " legs";
    }
};

// The trampoline: one neutral storage line + one neutral line per overridable
// virtual. `kingdom()` is bind_flat, so it is intentionally NOT overridden here.
struct PyAnimal : Animal {
    WELDER_PY_TRAMPOLINE(Animal);
    std::string speak() const override { WELDER_PY_OVERRIDE(speak); }
    int legs() const override { WELDER_PY_OVERRIDE(legs); }
};

// An *abstract* base with a pure virtual a Python subclass supplies. Because welder
// registers the concrete trampoline's default constructor (see construction_type),
// the base is constructible from Python and a subclass works; an unoverridden pure
// virtual raises at call time (nanobind/pybind11 behavior), not at construction.
struct
[[=welder::weld(welder::lang::py)]]
Shape {
    virtual ~Shape() = default;

    [[=welder::doc("The shape's area.")]]
    virtual double area() const = 0; // pure virtual — no C++ fallback

    // A concrete C++ method calling the pure virtual polymorphically.
    double scaled_area(double factor) const { return area() * factor; }
};

// This trampoline uses the *annotation* form: the [[=trampoline]] mark lets welder
// discover it by scanning Shape's namespace — no trampoline_for<Shape> needed.
struct [[=welder::rods::python::trampoline]] PyShape : Shape {
    WELDER_PY_TRAMPOLINE(Shape);
    double area() const override { WELDER_PY_OVERRIDE(area); }
};

} // namespace overridable

// Animal uses the explicit registration form (trampoline_for); Shape uses the
// annotation form above. Both discovery paths are thus exercised.
template <> constexpr std::meta::info
    welder::rods::python::trampoline_for<overridable::Animal> = ^^overridable::PyAnimal;

inline void register_overridable(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "overridable")};
    WELDER_TEST_WELDER::weld_namespace<^^overridable>(sub);
}