// LuaCATS stub backend test case + generator.
//
// The stub backend's whole value is the docstrings Lua drops at runtime, so this
// is a *dedicated* doc-rich case rather than a reuse of tests/common/cpp (whose
// cases exercise binding behavior and mostly carry no [[=welder::doc]] — the same
// reason doc.hpp is its own Python case). It exercises the surface a stub must
// render: class/field/method/return/param docs, constructors, an aggregate field
// constructor, welded-base inheritance, operators (arithmetic + the bitwise set
// LuaCATS can type; comparison/subscript are excluded — no `---@operator` form),
// scoped + unscoped enums, a free
// function, a namespace variable, the STL type map (vector/map/optional), and a
// nested namespace. WELDER_LUACATS_MAIN emits the ---@meta stub for `stubdemo`;
// the CTest golden-compares it (tests/luacats/stub.golden.lua).
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <welder/vocabulary.hpp>
#include <welder/rods/lua/luacats/module.hpp>

namespace [[=welder::doc("A tiny geometry module for the LuaCATS stub test.")]]
stubdemo {

/// (this `///` is invisible to reflection; use welder::doc below)
enum [[=welder::weld(welder::lang::lua)]] [[=welder::doc("Cardinal directions.")]]
Direction { North, East, South, West };

enum class [[=welder::weld(welder::lang::lua)]] [[=welder::doc("Named colors.")]]
Color { Red, Green = 2, Blue };

// weld_as renames the *type*: the stub must carry `Figure` at the declaration AND
// wherever Shape is referenced (here, as Circle's base) — exercising the type
// map's reference reconciliation, not just the declaration.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A shape.")]]
       [[=welder::weld_as(welder::lang::lua, "Figure")]]
Shape {
    [[=welder::doc("A human-readable label.")]] std::string label;
    [[=welder::doc("Number of spatial dimensions.")]] const std::uint32_t dims{2};

    Shape() = default;

    [[=welder::doc("The area of the shape.")]] [[=welder::returns("square units")]]
    virtual double area() const;
};

struct [[=welder::weld(welder::lang::lua)]]
       [[=welder::doc(R"(A circle.

A round shape with one radius.)")]]
Circle : Shape {
    [[=welder::doc("The radius, in units.")]] double radius;

    Circle() = default;
    [[=welder::doc("Construct from a radius.")]]
    Circle([[=welder::doc("the radius")]] double radius);

    double area() const;

    // Overloaded method: rendered as one `function` + a `---@overload` signature.
    [[=welder::doc("A scaled copy.")]]
    Circle scaled([[=welder::doc("uniform factor")]] double k) const;
    Circle scaled(double kx, double ky) const;

    [[=welder::doc("A scaled copy.")]]
    Circle operator*([[=welder::doc("the scale factor")]] double k) const;
    bool operator==(const Circle& rhs) const;
};

// Bitwise operators: exercise the Lua ≥ 5.3 metamethods the stub can also type
// (band/bor/bxor/bnot/shl/shr). ==/</<=/[] are deliberately NOT here — LuaCATS
// `---@operator` can't name them (they bind at runtime but have no stub form).
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A bit mask (exercises the bitwise metamethods).")]]
Mask {
    [[=welder::doc("The raw bits.")]] std::uint32_t bits{0};

    [[=welder::doc("Bitwise AND.")]] Mask operator&(const Mask& rhs) const;
    Mask operator|(const Mask& rhs) const;
    Mask operator^(const Mask& rhs) const;
    [[=welder::doc("Bitwise NOT.")]] Mask operator~() const;
    Mask operator<<([[=welder::doc("shift distance")]] unsigned n) const;
    Mask operator>>(unsigned n) const;
};

// Another type rename, this one reached only through *container* references:
// Polygon's `vector<Box>` and `map<string, Box>` must render `Rect[]` /
// `table<string, Rect>`, proving the reconciliation reaches inside the STL wrappers.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("Axis-aligned box (aggregate).")]]
       [[=welder::weld_as(welder::lang::lua, "Rect")]]
Box {
    [[=welder::doc("Width in units.")]] double width;
    [[=welder::doc("Height in units.")]] double height;
};

/// A polygon holding vertices and per-vertex labels.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A polygon.")]]
Polygon {
    [[=welder::doc("The corner points, as [x, y] pairs.")]]
    std::vector<Box> corners;
    [[=welder::doc("Optional name.")]] std::optional<std::string> name;
    [[=welder::doc("Named anchor points.")]] std::map<std::string, Box> anchors;
};

[[=welder::weld(welder::lang::lua)]] [[=welder::doc("Sum a list of areas.")]]
[[=welder::returns("the total area")]]
double total_area([[=welder::doc("the shapes' areas")]] const std::vector<double>& areas);

// Overloaded free function: one `function` def + a `---@overload` signature.
[[=welder::weld(welder::lang::lua)]] [[=welder::doc("Describe an area, optionally with a unit.")]]
std::string describe([[=welder::doc("the area")]] double area);
[[=welder::weld(welder::lang::lua)]]
std::string describe(double area, const std::string& unit);

namespace [[=welder::doc("Unit constants.")]] units {
[[=welder::weld(welder::lang::lua)]] [[=welder::doc("Radians in a full turn.")]]
constexpr double tau = 6.283185307179586;
} // namespace units

} // namespace stubdemo

WELDER_LUACATS_MAIN(stubdemo)
