// Negative-compile case (must FAIL to build): a resolved property whose name
// collides with a bound data member of the same class — compared by the
// case-normalized word sequence, so spelling-convention variants count too
// (diag::property_name_collision).
//
// Built by the `negcompile.property_shadows_member` CTest (WILL_FAIL).
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py)]] Widget {
    Widget() = default;
    int size{0}; // the bound field the property would shadow
    [[=welder::getter]] int get_size() const { return size; }
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Widget>(m);
}
