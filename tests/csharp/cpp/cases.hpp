#pragma once
// C#/.NET backend test cases (the Phase-1 slice).
//
// NOTE: unlike the Python/Lua backends — which bind the shared tests/common/cpp tree
// — the C# rod still uses a dedicated case file: the shared cases lean on features
// later phases add (operators, inheritance, virtuals, containers). Widening the
// shared cases with `lang::cs` markers is the per-phase completeness bar; this file
// is the GOLDEN anchor throughout (tests/csharp locks the emitted artifacts against
// it byte-for-byte).
//
// #included by gen.cpp (the WELDER_CSHARP_MAIN generator) after the welder
// vocabulary, and by the generated shim.cpp (which re-runs the same reflection).
#include <cstdint>
#include <stdexcept>
#include <string>
#include <welder/vocabulary.hpp>

namespace csharp_cases {

// --- enums (bind as C# `enum : <underlying>`, per-enumerator docs) -----------

enum class [[=welder::weld(welder::lang::cs)]]
[[=welder::doc("Primary display colors.")]]
Color {
    Red,   /**< The warm one. */
    Green, /**< The calm one. */
    Blue,
};

// A non-int underlying type: crosses (and mirrors) as `byte`.
enum class [[=welder::weld(welder::lang::cs)]] Level : std::uint8_t {
    Low = 1,
    High = 200,
};

// --- a class: fields (properties), ctors, overloads, accessors, Clone -------

struct [[=welder::weld(welder::lang::cs)]]
[[=welder::doc("A 2-D integer point.")]]
Point {
    /** Horizontal coordinate. */
    std::int32_t x{0};
    std::int32_t y{0};
    [[=welder::mark::no_reassign]] std::int32_t stamp{7}; // get-only property

    Point() = default;
    Point(std::int32_t x_, std::int32_t y_) : x{x_}, y{y_} {}

    std::int32_t sum() const { return x + y; }
    /** Move the point.
        @param dx horizontal delta
        @param dy vertical delta */
    void offset(std::int32_t dx, std::int32_t dy) { x += dx; y += dy; }
    void offset(std::int32_t d) { x += d; y += d; }   // an overload
    std::string label() const {                        // string return
        return "(" + std::to_string(x) + "," + std::to_string(y) + ")";
    }
    Color hue() const { return x == y ? Color::Green : Color::Red; }
    /** A translated copy.
        @returns the moved point */
    Point translated(std::int32_t dx, std::int32_t dy) const {
        return Point(x + dx, y + dy);
    }
    static Point origin() { return Point(0, 0); }
    void explode() const { throw std::out_of_range{"boom"}; } // error contract
    Point operator+(const Point& o) const { return Point(x + o.x, y + o.y); }
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
    // A method-backed property (getter/setter marks).
    [[=welder::getter]] std::int32_t depth() const { return depth_; }
    [[=welder::setter]] void depth(std::int32_t d) { depth_ = d; }

  private:
    std::int32_t depth_{0};
};

// --- an aggregate (synthesized field constructor) ----------------------------

struct [[=welder::weld(welder::lang::cs)]] Size {
    std::int32_t width{0};
    std::int32_t height{0};
};

// --- ownership / return policies ----------------------------------------------

struct [[=welder::weld(welder::lang::cs)]]
Holder {
    Holder() = default;

    // reference_internal: a live view aliasing the member; the C# view wrapper
    // pins this Holder (its __owner) so GC cannot finalize it under the view.
    [[=welder::return_policy(welder::rv::reference_internal)]]
    Point& item() { return item_; }

    // copy: an independent snapshot.
    [[=welder::return_policy(welder::rv::copy)]]
    Point& item_copy() { return item_; }

    // reference on a nullable pointer: a view, or C# null.
    [[=welder::return_policy(welder::rv::reference)]]
    Point* peek(bool give) { return give ? &item_ : nullptr; }

    std::int32_t item_x() const { return item_.x; }

  private:
    Point item_{1, 2};
};

// A factory pointer return under the default policy: the C# side adopts it.
[[=welder::weld(welder::lang::cs)]]
inline Point* make_point(std::int32_t x, std::int32_t y) {
    return new Point(x, y);
}

// --- the exception taxonomy ----------------------------------------------------

[[=welder::weld(welder::lang::cs)]]
inline void reject(std::int32_t v) {
    if (v < 0)
        throw std::invalid_argument{"negative"};
    throw std::overflow_error{"too big"};
}

// --- a class taking + holding welded types -----------------------------------

struct [[=welder::weld(welder::lang::cs)]]
Segment {
    Point start;
    Point end;

    Segment() = default;
    Segment(Point a, Point b) : start{a}, end{b} {} // welded (by-value) params
    std::int32_t span() const { return end.sum() - start.sum(); }
    bool degenerate() const { return start.sum() == end.sum(); } // bool return
};

// --- free functions (overloaded) + namespace variables -----------------------

[[=welder::weld(welder::lang::cs)]]
[[=welder::doc("Add two numbers.")]]
inline std::int32_t add(std::int32_t a, std::int32_t b) { return a + b; }

[[=welder::weld(welder::lang::cs)]]
inline std::string greet(std::string name) { return "hi " + name; }

[[=welder::weld(welder::lang::cs)]]
inline std::int32_t answer{42}; // mutable → a static get/set property

[[=welder::weld(welder::lang::cs)]]
inline constexpr double golden{1.618}; // const → get-only

// --- inheritance (base chain + an extra base as a view) ------------------------

struct [[=welder::weld(welder::lang::cs)]] Animal {
    Animal() = default;
    std::string kind() const { return "animal"; }
    std::int32_t age{1};
};

struct [[=welder::weld(welder::lang::cs)]] Legged {
    Legged() = default;
    std::int32_t legs{4};
};

struct [[=welder::weld(welder::lang::cs)]] Dog : Animal, Legged {
    Dog() = default;
    std::string bark() const { return "woof"; }
};

[[=welder::weld(welder::lang::cs)]]
inline std::int32_t age_of(const Animal& a) { return a.age; }

// --- a nested namespace (a static-class scope) --------------------------------

namespace inner {
[[=welder::weld(welder::lang::cs)]]
inline std::int32_t twice(std::int32_t v) { return 2 * v; }
} // namespace inner

} // namespace csharp_cases
