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
    // A data member's doc rides on its pybind11 property (`Circle.r.__doc__`).
    [[=welder::doc("The radius.")]] double r{0.0};

    Circle() = default;
    Circle(double radius) : r{radius} {}

    [[
      =welder::doc("Compute the area."),
      =welder::returns("the area")
    ]]
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

// --- const vs mutable field docs --------------------------------------------
// A const data member is bound read-only (def_readonly) — def_readwrite's setter
// would not compile — while a mutable one is read/write (def_readwrite). Both
// carry their field doc onto the property's __doc__.
struct
[[
  =welder::weld(welder::lang::py),
  =welder::doc("A labelled marker.")
]]
Marker {
    [[=welder::doc("The immutable id.")]] const int id{7};
    [[=welder::doc("A mutable note.")]] std::string note{};

    Marker() = default;
};

// --- free function + parameter docstrings -----------------------------------
[[
  =welder::weld(welder::lang::py),
  =welder::doc("Add two integers."),
  =welder::returns("their sum")
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

// Only a return doc, no summary and no parameter docs -> the docstring is just a
// Returns: block (no leading blank line).
[[
  =welder::weld(welder::lang::py),
  =welder::returns("the doubled value")
]]
int twice(int x) {
    return 2 * x;
}

// --- multiline / raw-string docstrings --------------------------------------
// A raw string literal is still a const char[N], so it flows through
// welder::doc verbatim: newlines, blank lines, quotes and backslashes all reach
// the Python __doc__. Function docs commonly carry multiline examples, so this
// path is load-bearing.
//
// The doc text is written indented to line up with the surrounding source; welder
// dedents it (PEP 257 / cleandoc semantics) so the indentation does NOT leak into
// __doc__ — while an example block's *relative* extra indentation is preserved.
struct
[[
  =welder::weld(welder::lang::py),
  =welder::doc(R"(
      A gadget.

      Example:
          >>> Gadget().tag
          0
  )")
]]
Gadget {
    int tag{0};
};

// Multiline summary, plus a multiline parameter doc and a multiline return doc,
// all indented for readability (dedented by welder): google_style then keeps each
// block's continuation lines indented under it.
[[
  =welder::weld(welder::lang::py),
  =welder::doc(R"(
      Combine two values.

      Detailed multiline
      description.)"),
  =welder::returns(R"(
      the combined result,
      described over two lines)")
]]
int combine(
    [[=welder::doc(R"(
        the first operand,
        spanning two lines)")]] int a,
    [[=welder::doc("the second operand")]] int b) {
    return a + b;
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
