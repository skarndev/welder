// Negative-compile case (must FAIL to build): a method whose signature names a
// nested type that does NOT participate — here mark::exclude'd off its outer —
// cannot be bound: the nested-type sweep never registers Gearbox, so pybind11
// would have no converter for it and the method would be dead at call time.
// welder's bindability gate (whose registration oracle mirrors the sweep exactly:
// detail::nested_type_registered) turns this into a hard error.
//
// Built by the `negcompile.nested_excluded_in_signature` CTest, which expects
// failure. The TU is otherwise valid, so the only thing that can fail is the
// bindability static_assert.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py)]] Engine {
    // Excluded -> the sweep skips it -> nothing registers it.
    struct [[=welder::mark::exclude]] Gearbox {
        int gears{5};
    };

    Gearbox box() const { return {}; } // names the unregistered nested type
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Engine>(m);
}