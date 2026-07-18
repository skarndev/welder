// Negative-compile case (must FAIL to build): a participating [[=welder::setter]]
// whose property has no participating getter — welder binds no write-only
// properties (diag::setter_without_getter). The TU is otherwise valid.
//
// Built by the `negcompile.property_setter_without_getter` CTest (WILL_FAIL).
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py)]] Widget {
    Widget() = default;
    [[=welder::setter]] void set_size(int v) { size_ = v; } // no getter anywhere

  private:
    int size_{0};
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Widget>(m);
}
