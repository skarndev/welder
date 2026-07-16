// Negative-compile case (must FAIL to build): two participating member aliases
// naming the SAME target would register it twice — a framework load error —
// so the carriage diagnoses the duplicate at compile time (the class-scope twin
// of the namespace-alias `alias_duplicate` case). mark::exclude one of them to
// keep a single registration.
//
// Built by the `negcompile.member_alias_duplicate` CTest, which expects failure.
// The TU is otherwise valid, so the only thing that can fail is the
// sole_member_alias_of_target static_assert.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace vendor {
struct Gear { // deliberately not welded: both aliases would register it
    int teeth{12};
};
} // namespace vendor

struct [[=welder::weld(welder::lang::py)]] Machine {
    using Gear = vendor::Gear;
    using Cog = vendor::Gear; // same target, also participating -> diagnosed
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Machine>(m);
}