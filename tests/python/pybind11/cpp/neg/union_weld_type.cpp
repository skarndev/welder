// Negative-compile case (must FAIL to build): unions cannot be welded, ever —
// C++ has no way to observe which union member is active, so generated member
// accessors would read inactive members (undefined behavior). weld_type on a
// union is a designed hard error whose message names the fix: std::variant.
//
// Built by the `negcompile.union_weld_type` CTest, which expects failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

union [[=welder::weld(welder::lang::py)]] Payload {
    int i;
    float f;
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Payload>(m);
}