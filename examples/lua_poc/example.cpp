// Whole-module binding for Lua in one declaration: WELDER_MODULE builds a
// loadable Lua C module straight out of an annotated namespace — no hand-written
// luaopen_, no per-type bind<> calls. The same welder core and annotations that
// drive the Python backends drive this one; only the backend header differs.
//
// Header-only consumption (welder::headers); the macro and backend come from
// <welder/rods/lua/sol2/module.hpp>. Build with welder_sol2_add_module() so
// the output is a `require`-able `shapes_lua.so` (see this dir's CMakeLists.txt).
//
// [[=welder::doc]] annotations have no runtime home in Lua (no __doc__), but they
// are kept in the source: they are the material a future LuaCATS (---@meta) stub
// emitter would surface to the Lua language server.
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <sol/sol.hpp>
#include <welder/rods/lua/sol2/module.hpp>

// The namespace name doubles as the module name (require "shapes_lua").
namespace
[[=welder::doc("A small shapes module built by welder, for Lua.")]]
shapes_lua {

enum class
[[
  =welder::weld(welder::lang::lua),
  =welder::doc("How to treat a shape's edges.")
]]
Edge { Sharp, Round };

struct
[[
  =welder::weld(welder::lang::lua),
  =welder::doc("An axis-aligned rectangle.")
]]
Rect {
    double w{0.0};
    double h{0.0};

    Rect() = default;
    Rect(double width, double height) : w{width}, h{height} {}

    [[=welder::doc("The area of the rectangle.")]]
    double area() const { return w * h; }

    // Member operators map to Lua metamethods (__add, __eq).
    Rect operator+(const Rect& o) const { return Rect{w + o.w, h + o.h}; }
    bool operator==(const Rect& o) const { return w == o.w && h == o.h; }
};

[[
  =welder::weld(welder::lang::lua),
  =welder::doc("Scale a length by a factor."),
  =welder::returns("the scaled length")
]]
double scale(
    [[=welder::doc("the length to scale")]] double length,
    [[=welder::doc("the multiplier")]] double factor) {
    return length * factor;
}

[[=welder::weld(welder::lang::lua), =welder::doc("The module's version.")]]
const int VERSION{1};

} // namespace shapes_lua

// One line emits luaopen_shapes_lua and binds the whole namespace into the module
// table. The trailing block is optional post-glue: welder has already bound the
// namespace into `module`; here we add a hand-written field.
WELDER_MODULE(shapes_lua, sol2) {
    module["BUILT_BY"] = "welder";
}
