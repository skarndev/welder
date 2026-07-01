#pragma once
// trust_bindable cases — mirrors tests/test_trust.py (same sections, same order).
//
// welder's bindability gate rejects a non-welded program-defined type, because it
// cannot see a registration made outside welder. When the user registers such a
// type with pybind11 by hand, the two trust_bindable escape hatches vouch for it so
// the member still binds:
//   * [[=welder::mark::trust_bindable]] on the member — trust this member's type.
//   * welder::trust_bindable<T> = true                 — trust T everywhere.
// Compile counterpart of the negcompile cases: without trust, using an unregistered
// type is a hard error (tests/pybind11/cpp/neg/); with it, it compiles and binds.
//
// #included by bindings.cpp *after* the welder vocabulary and the pybind11 backend
// are in scope; this header deliberately does not include them itself.
#include <vector>

// A plain type welder never sees welded; register_trust hand-registers it with
// pybind11 before binding the structs that use it.
struct Handmade {
    int n{0};
};

// --- member-mark trust: the mark trusts this member's type, for this member ----

struct
[[=welder::weld(welder::lang::py)]]
TrustsMember {
    // trusted via the member mark -> bound (welder skips the bindability gate here)
    [[=welder::mark::trust_bindable]]
    Handmade item;

    int count{0};

    // the mark also trusts a *method's* whole signature: this returns the
    // unregistered-to-welder type, which would otherwise fail the gate.
    [[=welder::mark::trust_bindable]]
    Handmade make(int n) const { return Handmade{n}; }
};

// --- type-level trust: trust_bindable<T> trusts T everywhere it appears --------

struct Handmade2 {
    int n{0};
};
// Vouch for Handmade2 wherever it appears (member, container element, ...).
template <>
inline constexpr bool welder::trust_bindable<Handmade2> = true;

struct
[[=welder::weld(welder::lang::py)]]
TrustsType {
    // trusted via the type-level point (no per-member mark) -> bound
    Handmade2 item;

    // also cleared: the wrapper table recurses vector -> Handmade2, a trusted leaf
    std::vector<Handmade2> many;

    int count{0};
};

inline void register_trust(pybind11::module_& m) {
    namespace py = pybind11;
    // Hand-register the trusted types BEFORE binding the structs that use them:
    // pybind11 needs the class registered when welder's def_readwrite runs.
    py::class_<Handmade>(m, "Handmade")
        .def(py::init<>())
        .def_readwrite("n", &Handmade::n);
    py::class_<Handmade2>(m, "Handmade2")
        .def(py::init<>())
        .def_readwrite("n", &Handmade2::n);
    welder::pybind11::bind<TrustsMember>(m);
    welder::pybind11::bind<TrustsType>(m);
}
