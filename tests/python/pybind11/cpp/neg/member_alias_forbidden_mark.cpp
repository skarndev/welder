// Negative-compile case (must FAIL to build): only weld_as / exclude / include /
// only may sit on a member type alias — participation follows the outer's policy
// and the bindability gate, so `weld` / `policy` / `doc` / trust / call-policy
// marks would be silently inert there; welder diagnoses them instead (the
// class-scope twin of the namespace-alias `alias_forbidden_mark` case).
//
// Built by the `negcompile.member_alias_forbidden_mark` CTest, which expects
// failure. The TU is otherwise valid, so the only thing that can fail is the
// member_alias_marks_admissible static_assert.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace vendor {
struct Gear {
    int teeth{12};
};
} // namespace vendor

struct [[=welder::weld(welder::lang::py)]] Machine {
    // doc belongs on the target type, not the alias -> diagnosed.
    using Gear [[=welder::doc("the drive gear")]] = vendor::Gear;
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Machine>(m);
}