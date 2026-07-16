// Negative-compile case (must FAIL to build): a member alias's registration is
// visible only to the REGISTERING class's own members (the scope-aware oracle)
// — an alias is unrecoverable from the type it names, so ANOTHER class naming
// the same vendor type in a signature still fails the gate. The remedy across
// classes is trust_bindable (or welding the vendor type properly).
//
// Built by the `negcompile.member_alias_cross_class` CTest, which expects
// failure. The TU is otherwise valid, so the only thing that can fail is the
// bindability static_assert on Meter::probe.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace vendor {
struct Sensor { // unwelded; registered only through Panel's member alias
    int range{5};
};
} // namespace vendor

namespace cross_ns {

struct [[=welder::weld(welder::lang::py)]] Panel {
    using Sensor = vendor::Sensor; // registers vendor::Sensor as Panel.Sensor
    vendor::Sensor s{};            // fine: Panel's own members see the alias
};

struct [[=welder::weld(welder::lang::py)]] Meter {
    vendor::Sensor probe() const { return {}; } // NOT fine: another class's
                                                // alias is invisible here
};

} // namespace cross_ns

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^cross_ns>(m);
}