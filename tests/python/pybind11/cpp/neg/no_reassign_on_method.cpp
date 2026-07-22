// Negative-compile case (must FAIL to build): mark::no_reassign on a METHOD is a
// designed hard error. The mark forces a nonstatic DATA MEMBER's read-only binding
// (mutate in place, no whole-attribute rebind) — it has no meaning on a member
// function, so welder diagnoses it rather than silently ignoring it. Caught by the
// class-member sweep in carriage.hpp (bind_members).
//
// Built by the `negcompile.no_reassign_on_method` CTest, which expects failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py)]] Widget {
    [[=welder::mark::no_reassign]] int value() const { return 1; }
    int data{0};
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Widget>(m);
}
