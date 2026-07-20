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
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <welder/vocabulary.hpp>
#include <welder/rods/lua/luacats/module.hpp>

// A vendor-style type nobody can annotate: registered through Gauge's MEMBER
// ALIAS below, so references to it (face_plate's return) must be reconciled to
// the alias's dotted declaration name.
namespace stub_vendor {

struct Plate {
    double thickness{1.0};
};

} // namespace stub_vendor

namespace [[=welder::doc("A tiny geometry module for the LuaCATS stub test.")]]
stubdemo {

/// (this `///` is invisible to reflection; use welder::doc below)
enum [[=welder::weld(welder::lang::lua)]] [[=welder::doc("Cardinal directions.")]]
Direction { North, East, South, West };

// Per-enumerator docs: LuaCATS has no per-member tag, but the language server
// attaches a `---` comment above a field in the `---@enum` table to that member
// (hover/completion). An undocumented enumerator (Blue) gets no comment.
enum class [[=welder::weld(welder::lang::lua)]] [[=welder::doc("Named colors.")]]
Color {
    Red [[=welder::doc("the warm primary")]],
    Green [[=welder::doc("chlorophyll")]] = 2,
    Blue,
};

// weld_as renames the *type*: the stub must carry `Figure` at the declaration AND
// wherever Shape is referenced (here, as Circle's base) — exercising the type
// map's reference reconciliation, not just the declaration.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A shape.")]]
       [[=welder::weld_as(welder::lang::lua, "Figure")]]
Shape {
    [[=welder::doc("A human-readable label.")]] std::string label;
    [[=welder::doc("Number of spatial dimensions.")]] const std::uint32_t dims{2};

    Shape() = default;
    virtual ~Shape() = default;

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

// FREE operators anchored on a welded type. The anchor-on-the-left form renders
// like a member `---@operator` (operand = the second parameter); the REFLECTED
// form (Impulse on the right) is runtime-only — `---@operator` types self as
// the left operand — and is dropped, like the eq/lt/le slots. The spaceship
// synthesizes runtime __lt/__le only, so it emits nothing here either.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A scalable impulse (exercises free operators).")]]
Impulse {
    [[=welder::doc("The magnitude.")]] double mag{0.0};
    std::strong_ordering operator<=>(const Impulse& rhs) const;
};
Impulse operator*(const Impulse& i, double k);
Impulse operator*(double k, const Impulse& i);

// Method-backed properties (getter/setter marks): a paired accessor renders as
// a plain ---@field of the getter's return type; a lone getter adds the same
// (read-only) note a const data member gets; an explicit name is verbatim.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A throttle (exercises properties).")]]
Throttle {
    Throttle() = default;
    [[=welder::getter]] [[=welder::doc("The lever position, 0..1.")]]
    double get_position() const { return position_; }
    [[=welder::setter]] void set_position(double p) { position_ = p; }
    [[=welder::getter]] bool is_open() const { return position_ > 0.0; }
    [[=welder::getter("ratio")]] double raw() const { return position_; }

  private:
    double position_{0.0};
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

// An aggregate with an NSDMI suffix: the omissible fields render as `?`
// optional parameters in the synthesized constructor's signature — the stub's
// spelling of the runtime rods' per-arity overloads.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A splash screen (NSDMI-defaults aggregate).")]]
Splash {
    [[=welder::doc("Window caption.")]] std::string caption;
    [[=welder::doc("Display duration in seconds.")]] double seconds{2.5};
};

/// A polygon holding vertices and per-vertex labels.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A polygon.")]]
Polygon {
    [[=welder::doc("The corner points, as [x, y] pairs.")]]
    std::vector<Box> corners;
    [[=welder::doc("Optional name.")]] std::optional<std::string> name;
    [[=welder::doc("Named anchor points.")]] std::map<std::string, Box> anchors;
};

// Class-NESTED types: a member class and a member enum resolve under the outer
// (no weld of their own — the outer's policy + their marks) and declare under
// its dotted name (stubdemo.Gauge.Needle / stubdemo.Gauge.Range), matching the
// sol2 runtime placement Outer.Inner; their blocks render after the outer's.
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A measuring gauge.")]]
Gauge {
    struct [[=welder::doc("The needle position.")]] Needle {
        [[=welder::doc("Angle in degrees.")]] double angle{0.0};
    };
    enum class [[=welder::doc("Operating range.")]] Range { Low, High };

    // A MEMBER ALIAS registering an unwelded vendor type nested here
    // (stubdemo.Gauge.Face): references to stub_vendor::Plate — face_plate's
    // return below — must render the alias's dotted name via the rename table.
    using Face = stub_vendor::Plate;

    [[=welder::doc("The current needle.")]] Needle needle;

    [[=welder::doc("Select the range.")]]
    void set_range([[=welder::doc("the new range")]] Range r);

    [[=welder::doc("The front plate.")]]
    stub_vendor::Plate face_plate() const;
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

// A class-template instantiation, welded through its namespace-scope alias — the
// alias is both the C++ spelling and the stub's class name (stubdemo.Pair).
template <class T>
struct [[=welder::weld(welder::lang::lua)]] [[=welder::doc("A homogeneous pair.")]]
Duo {
    [[=welder::doc("First of the pair.")]] T first{};
    [[=welder::doc("Second of the pair.")]] T second{};
};

using Pair = Duo<double>;

} // namespace stubdemo

WELDER_LUACATS_MAIN(stubdemo)
