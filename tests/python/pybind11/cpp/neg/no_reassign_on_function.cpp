// Negative-compile case (must FAIL to build): mark::no_reassign on a FREE FUNCTION
// is a designed hard error. The mark forces a nonstatic DATA MEMBER's read-only
// binding — it has no meaning on a function, so the semi-manual weld_function entry
// point diagnoses it (the whole-namespace weld_namespace route is guarded the same
// way in the namespace walk).
//
// Built by the `negcompile.no_reassign_on_function` CTest, which expects failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

[[=welder::weld(welder::lang::py)]] [[=welder::mark::no_reassign]]
int compute() { return 42; }

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_function<^^compute>(m);
}
