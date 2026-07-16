// Negative-compile case (must FAIL to build): a bound data member whose type is
// a union fails the bindability gate with the union-specific diagnostic — even
// when the union is itself weld-marked (no sweep will ever register a union, so
// the mark must not count as a registration promise; this used to compile clean
// and fail only at runtime). The remedies the message names: std::variant, safe
// accessor functions, mark::exclude, or trust_bindable + hand registration.
//
// Built by the `negcompile.union_member` CTest, which expects failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

union [[=welder::weld(welder::lang::py)]] Payload {  // the weld must not help
    int i;
    float f;
};

struct [[=welder::weld(welder::lang::py)]] Packet {
    Payload payload{};
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Packet>(m);
}