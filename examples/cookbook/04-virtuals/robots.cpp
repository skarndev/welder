// Cookbook 04 — virtuals: let Python subclasses override C++ virtual methods.
//
// A virtual method needs a TRAMPOLINE — a C++ subclass with one override per
// virtual that forwards into Python. welder automates everything around it
// (registration, coverage checking, dispatch plumbing via the neutral
// WELDER_PY_TRAMPOLINE / WELDER_PY_OVERRIDE macros), but the trampoline
// declarations themselves are written by hand here — recipe 05 generates them
// instead. docs/content/cookbook/virtuals.md walks through this file.
#include <string>

#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/rod.hpp>
#include <welder/rods/python/pybind11/trampoline.hpp> // virtual-override support

namespace robots {

// A polymorphic base with C++ default implementations. status() is non-virtual
// and calls the virtuals polymorphically — observing it from Python proves a
// C++ call dispatches into a Python override.
struct
[[=welder::weld(welder::lang::py), =welder::doc("A programmable robot.")]]
Robot {
    virtual ~Robot() = default;

    [[=welder::doc("The robot's callsign.")]]
    virtual std::string name() const { return "unit"; }

    virtual int speed() const { return 1; }

    // Deliberately bound FLAT: callable from Python, but not overridable — it
    // stays out of the trampoline (C++ never dispatches it back into Python).
    [[=welder::rods::python::bind_flat]]
    virtual std::string vendor() const { return "ACME"; }

    std::string status() const {
        return name() + " @ speed " + std::to_string(speed());
    }
};

// The hand-written trampoline: one neutral macro line, then one line per
// overridable virtual. `vendor()` is bind_flat, so it is intentionally absent —
// welder statically checks that everything else is covered.
struct PyRobot : Robot {
    WELDER_PY_TRAMPOLINE(PyRobot, Robot);
    std::string name() const override { WELDER_PY_OVERRIDE(name); }
    int speed() const override { WELDER_PY_OVERRIDE(speed); }
};

// An ABSTRACT base: the pure virtual has no C++ fallback. welder registers the
// trampoline as the construction type, so Python subclasses instantiate fine;
// an unoverridden duty() raises at call time.
struct
[[=welder::weld(welder::lang::py), =welder::doc("A robot mission.")]]
Mission {
    virtual ~Mission() = default;

    [[=welder::doc("What this mission does.")]]
    virtual std::string duty() const = 0;

    std::string briefing() const { return "mission: " + duty(); }
};

// This trampoline uses the ANNOTATION discovery form — the [[=trampoline]] mark
// lets welder find it by scanning Mission's namespace, no registration needed.
struct [[=welder::rods::python::trampoline]] PyMission : Mission {
    WELDER_PY_TRAMPOLINE(PyMission, Mission);
    std::string duty() const override { WELDER_PY_OVERRIDE(duty); }
};

} // namespace robots

// Robot uses the EXPLICIT registration form (works across namespaces and
// disambiguates multiple candidates); Mission was discovered by annotation above.
template <>
constexpr std::meta::info
    welder::rods::python::trampoline_for<robots::Robot> = ^^robots::PyRobot;

PYBIND11_MODULE(robots, m) {
    m.doc() = "welder cookbook 04 - virtual overriding";
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^robots>(m);
}