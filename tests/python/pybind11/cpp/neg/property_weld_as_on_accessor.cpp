// Negative-compile case (must FAIL to build): [[=welder::weld_as]] on an
// accessor-marked function — the accessor's explicit name IS the property's
// rename tool, so the two would fight (diag::accessor_weld_as_conflict).
//
// Built by the `negcompile.property_weld_as_on_accessor` CTest (WILL_FAIL).
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py)]] Widget {
    Widget() = default;
    [[=welder::getter, =welder::weld_as("size")]]
    int get_size() const { return size_; }

  private:
    int size_{0};
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Widget>(m);
}
