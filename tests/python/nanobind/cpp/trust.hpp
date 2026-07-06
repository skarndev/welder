#pragma once
// trust_bindable cases (nanobind) — mirrors tests/test_trust.py (same sections,
// same order).
//
// welder's bindability gate rejects a non-welded program-defined type, because it
// cannot see a registration made outside welder. When the user registers such a
// type with nanobind by hand, the two trust_bindable escape hatches vouch for it so
// the member still binds:
//   * [[=welder::mark::trust_bindable]] on the member — trust this member's type.
//   * welder::trust_bindable<T> = true                 — trust T everywhere.
// Compile counterpart of the negcompile cases: without trust, using an unregistered
// type is a hard error (tests/nanobind/cpp/neg/); with it, it compiles and binds.
//
// The nanobind counterpart of tests/pybind11/cpp/trust.hpp: same cases and submodule
// (`trust`), against nanobind's hand-registration API (nb::class_ + def_rw).
//
// #included by bindings.cpp after the welder vocabulary and the nanobind backend.
#include <vector>

namespace trust {

// A plain type welder never sees welded; register_trust hand-registers it with
// nanobind before binding the structs that use it.
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

struct
[[=welder::weld(welder::lang::py)]]
TrustsType {
    // trusted via the type-level point (no per-member mark) -> bound
    Handmade2 item;

    // also cleared: the wrapper table recurses vector -> Handmade2, a trusted leaf
    std::vector<Handmade2> many;

    int count{0};
};

} // namespace trust

// Vouch for trust::Handmade2 wherever it appears (member, container element, ...).
// The specialization of welder::trust_bindable must sit outside namespace `trust`
// (it specializes a member of namespace `welder`), so it follows the block above.
template <>
inline constexpr bool welder::trust_bindable<trust::Handmade2> = true;

inline void register_trust(nanobind::module_& m) {
    namespace nb = nanobind;
    auto sub{m.def_submodule("trust")};
    // Hand-register the trusted types into the submodule BEFORE binding the structs
    // that use them: nanobind needs the class registered when def_rw runs.
    nb::class_<trust::Handmade>(sub, "Handmade")
        .def(nb::init<>())
        .def_rw("n", &trust::Handmade::n);
    nb::class_<trust::Handmade2>(sub, "Handmade2")
        .def(nb::init<>())
        .def_rw("n", &trust::Handmade2::n);
    WELDER_TEST_WELDER::weld_namespace<^^trust>(sub);
}
