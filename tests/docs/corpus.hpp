#pragma once
// The C++ docs-pipeline corpus — everything the doc walker + Doxygen shadow
// emitter must handle, in one namespace. The golden test (expected_atelier.hpp)
// locks the exact generated output. Deliberately std-free member types (int
// only) so the golden file does not depend on how the compiler displays library
// type names.
//
// What it exercises, section by section:
//   * Extent   — an UNWELDED class (the core point: no weld gate for C++ docs),
//                field docs, cxx_doc-excluded + all-excluded members, a value
//                constructor with param docs, const / static methods, returns,
//                a member operator, a cxx_doc-excluded method.
//   * Panel    — welded for py under policy::opt_in: both are ignored by docs
//                (public = documented; policy shapes the *binding* surface).
//   * Named    — inheritance: the public base shows on the class head, nothing
//                is flattened.
//   * Heading  — scoped enum with a documented enumerator (surfaced as ///<,
//                which binding backends cannot express) and a cxx_doc-excluded
//                one. Level — unscoped enum.
//   * scale    — free function with param docs + returns.
//   * max_side — documented const variable (variable docs are binding-dropped,
//                C++-docs-surfaced); instances — undocumented mutable variable.
//   * math     — documented nested namespace; detail — excluded wholesale;
//                empty_ns — pruned (nothing documentable).
//
// #included by gen.cpp after the welder vocabulary.

namespace
[[=welder::doc("A small workshop: the docs-pipeline test corpus.")]]
atelier {

struct
[[=welder::doc("A plain C++ extent; never bound to Python or Lua.")]]
Extent {
    [[=welder::doc("width in pixels")]] int w{0};
    int h{0};
    [[=welder::mark::exclude(welder::lang::cxx_doc)]] int scratch{0};
    [[=welder::mark::exclude]] int cache{0};

    Extent() = default;

    [[=welder::doc("Build an extent from both sides.")]]
    Extent(
        [[=welder::doc("width in pixels")]] int w_,
        [[=welder::doc("height in pixels")]] int h_) : w{w_}, h{h_} {}

    [[
      =welder::doc("The covered surface."),
      =welder::returns("w times h")
    ]]
    int area() const { return w * h; }

    [[=welder::doc("A square extent.")]]
    static Extent square(int side) { return Extent{side, side}; }

    Extent operator+(const Extent& o) const { return Extent{w + o.w, h + o.h}; }

    [[=welder::mark::exclude(welder::lang::cxx_doc)]]
    int secret() const { return 0; }
};

struct
[[
  =welder::weld(welder::lang::py),
  =welder::policy::opt_in,
  =welder::doc("Bound to Python opt-in; documented in C++ regardless.")
]]
Panel {
    int rows{0};
    [[=welder::mark::include(welder::lang::py)]] int cols{0};
};

struct
[[=welder::doc("An extent with an id (public base shown, not flattened).")]]
Named : Extent {
    int id{0};
};

enum class [[=welder::doc("Compass headings (scoped).")]] Heading {
    North [[=welder::doc("toward the top of the map")]],
    East,
    South,
    West [[=welder::mark::exclude(welder::lang::cxx_doc)]],
};

enum [[=welder::doc("Signal levels (unscoped).")]] Level {
    Low,
    High,
};

[[
  =welder::doc("Scale a value by a factor."),
  =welder::returns("value times factor")
]]
inline int scale(
    [[=welder::doc("the value to scale")]] int value,
    [[=welder::doc("the multiplier")]] int factor) {
    return value * factor;
}

[[=welder::doc("The largest supported extent side, in pixels.")]]
inline constexpr int max_side{4096};

inline int instances{0};

namespace [[=welder::doc("Numeric helpers.")]] math {

[[=welder::returns("the absolute value")]]
inline int magnitude(int v) { return v < 0 ? -v : v; }

} // namespace math

namespace [[=welder::mark::exclude(welder::lang::cxx_doc)]] detail {

inline int hidden_counter{0};

} // namespace detail

namespace empty_ns {}

} // namespace atelier
