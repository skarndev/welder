// Negative-compile case (must FAIL to build): a [[=welder::getter]] on a
// NON-const member function — a property read must not mutate, and the const
// requirement keeps every rod on its typed accessor path
// (diag::malformed_getter). The TU is otherwise valid.
//
// Built by the `negcompile.property_nonconst_getter` CTest (WILL_FAIL).
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py)]] Widget {
    Widget() = default;
    [[=welder::getter]] int get_size() { return size_; } // not const

  private:
    int size_{0};
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Widget>(m);
}
