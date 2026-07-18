// Negative-compile case (must FAIL to build): a [[=welder::getter]] on a
// VIRTUAL member function — a property object under the method's name would
// silently break Python's by-name override dispatch, so the combination is a
// designed error (diag::virtual_property_accessor), not a subtle misbinding.
// The type opts out of the trampoline gate with bind_flat so the only failable
// diagnostic is the accessor one.
//
// Built by the `negcompile.property_virtual_accessor` CTest (WILL_FAIL).
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py),
         =welder::rods::python::bind_flat]] Widget {
    Widget() = default;
    virtual ~Widget() = default;
    [[=welder::getter]] virtual int get_size() const { return 0; }
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Widget>(m);
}
