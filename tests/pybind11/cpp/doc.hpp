#pragma once
// Documentation — mirrors tests/test_doc.py (same sections, same order). Exercises
// the [[=welder::doc]] annotation across a class, its methods, free functions and
// their parameters, and a namespace docstring, plus the whole-module entry point
// welder::pybind11::build_module (pre/post hooks + namespace-doc adoption).
//
// Everything lives in the top-level namespace `documented` so it can be bound as
// one module via build_module (which requires a top-level namespace). It is bound
// into a `documented` submodule of the test module.
//
// #included by bindings.cpp after the welder vocabulary + pybind11 backend.
#include <string>

namespace
[[=welder::doc("The documented sample namespace.")]]
documented {

// --- class + method docstrings ----------------------------------------------
struct
[[
  =welder::weld(welder::lang::py),
  =welder::doc("A circle.")
]]
Circle {
    double r{0.0};

    Circle() = default;
    Circle(double radius) : r{radius} {}

    [[=welder::doc("Compute the area.")]]
    double area() const {
        return 3.14159 * r * r;
    }

    [[=welder::doc("The unit circle.")]]
    static Circle unit() {
        return Circle{1.0};
    }

    // undocumented method
    double circumference() const {
        return 2 * 3.14159 * r;
    }
};

// --- free function + parameter docstrings -----------------------------------
[[
  =welder::weld(welder::lang::py),
  =welder::doc("Add two integers.")
]]
int add(
    [[=welder::doc("left operand")]] int a,
    [[=welder::doc("right operand")]] int b) {
    return a + b;
}

// Documented function, but no parameter docs -> docstring is just the summary
// (no Args: block).
[[
  =welder::weld(welder::lang::py),
  =welder::doc("Negate a value.")
]]
int negate(int x) {
    return -x;
}

// --- variable doc is ignored (no module/attr __doc__ in Python) -------------
[[
  =welder::weld(welder::lang::py),
  =welder::doc("ignored on a variable")
]]
inline constexpr int ANSWER{42};

} // namespace documented

inline void register_doc(pybind11::module_& m) {
    // Bind the whole namespace as a submodule via build_module, exercising the
    // pre/post hooks and the namespace -> module docstring adoption. The hooks
    // drop marker attributes the Python side checks for.
    auto sub{m.def_submodule("documented")};
    welder::pybind11::build_module<^^documented>(
        sub,
        [](pybind11::module_& mm) { mm.attr("pre_marker") = 1; },
        [](pybind11::module_& mm) { mm.attr("post_marker") = 2; });
}
