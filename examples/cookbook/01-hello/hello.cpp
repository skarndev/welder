// Cookbook 01 — hello: one of everything.
//
// The four kinds of entity welder welds individually — a type, an enum, a free
// function and a namespace variable — each registered with its own weld_* call
// (the semi-manual route: you keep the module layout, welder does the members).
// docs/content/cookbook/hello.md walks through this file.
#include <cmath>
#include <string>

#include <welder/vocabulary.hpp> // the annotation vocabulary (welder is header-only)

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // std::string crosses via pybind11's STL casters
#include <welder/rods/python/pybind11/rod.hpp>

namespace hello {

// A type: default policy `automatic` binds every public member unless excluded.
// Vec2 is a baseless aggregate whose fields all bind, so welder also synthesizes
// the positional constructor Vec2(x, y). Member operators map to Python's
// __add__ / __eq__; `length` is an ordinary method.
struct
[[=welder::weld(welder::lang::py), =welder::doc("A 2-D vector.")]]
Vec2 {
    double x{0.0};
    double y{0.0};

    [[=welder::doc("The Euclidean length.")]]
    double length() const { return std::sqrt(x * x + y * y); }

    Vec2 operator+(const Vec2& o) const { return Vec2{x + o.x, y + o.y}; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

// An enum: welded enums bind as a Python enum.IntEnum (scoped enums stay
// Color.Red — an unscoped enum would also export its values onto the module).
enum class
[[=welder::weld(welder::lang::py), =welder::doc("A primary color.")]]
Color { Red, Green, Blue };

// A free function: parameter names become Python keyword arguments, and doc /
// returns render as a Google-style docstring.
[[
  =welder::weld(welder::lang::py),
  =welder::doc("The midpoint of two vectors."),
  =welder::returns("the point halfway between a and b")
]]
Vec2 midpoint(
    [[=welder::doc("one endpoint")]] const Vec2& a,
    [[=welder::doc("the other endpoint")]] const Vec2& b) {
    return Vec2{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
}

// Namespace variables: a const/constexpr one binds as a value snapshot; a mutable
// one becomes a LIVE module property over the C++ global (read AND write from
// Python reach this int).
[[=welder::weld(welder::lang::py), =welder::doc("Circle constant.")]]
inline constexpr double TAU{6.283185307179586};

[[=welder::weld(welder::lang::py), =welder::doc("How many midpoints were taken.")]]
inline int midpoint_count{0};

// Mutates the global from C++, so the check can observe the live property.
[[=welder::weld(welder::lang::py)]]
void count_midpoint() { ++midpoint_count; }

} // namespace hello

PYBIND11_MODULE(hello, m) {
    m.doc() = "welder cookbook 01 - one of everything";
    // The one public entry point, parameterized on the rod (the pybind11 backend).
    using weld = welder::welder<welder::rods::pybind11::rod<>>;
    weld::weld_type<hello::Vec2>(m);
    weld::weld_type<hello::Color>(m); // weld_type dispatches enums to the enum path
    weld::weld_function<^^hello::midpoint>(m);
    weld::weld_function<^^hello::count_midpoint>(m);
    weld::weld_variable<^^hello::TAU>(m);
    weld::weld_variable<^^hello::midpoint_count>(m);
}