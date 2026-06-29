// Whole-module binding in one declaration: WELDER_MODULE builds an importable
// Python module straight out of an annotated namespace — no PYBIND11_MODULE, no
// per-type bind<> calls. Also shows [[=welder::doc]] flowing through to Python
// __doc__ (namespace -> module doc, class/function docs, Google-style arg docs).
//
// Header-only consumption (welder::headers); the macro and backend come from
// <welder/backends/pybind11.hpp>.
#include <cstdint>
#include <string>

#include <welder/welder.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/backends/pybind11.hpp>

// The namespace name doubles as the module name (import shapes). Its doc becomes
// the module docstring.
namespace [[=welder::doc("A small shapes module built by welder.")]]
shapes {

struct [[=welder::weld(welder::lang::py)]] [[=welder::doc("An axis-aligned rectangle.")]]
Rect {
    double w{0.0};
    double h{0.0};

    Rect() = default;
    Rect(double width, double height) : w{width}, h{height} {}

    [[=welder::doc("The area of the rectangle.")]]
    double area() const { return w * h; }
};

[[=welder::weld(welder::lang::py)]] [[=welder::doc("Scale a length by a factor.")]]
double scale([[=welder::doc("the length to scale")]] double length,
             [[=welder::doc("the multiplier")]] double factor) {
    return length * factor;
}

} // namespace shapes

// One line emits the PyInit_shapes entry point and binds the whole namespace into
// it. The trailing block is optional post-glue: welder has already bound the
// namespace into `module`; here we add a hand-written constant.
WELDER_MODULE(shapes, pybind11) {
    module.attr("VERSION") = "1.0";
}
