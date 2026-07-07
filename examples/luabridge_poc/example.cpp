// Whole-module binding for Lua in one declaration, via the LuaBridge3 rod: the same
// annotated namespace, the same WELDER_MODULE macro, only the backend header
// differs from the sol2 example (examples/lua_poc). welder builds a loadable Lua C
// module — no hand-written luaopen_, no per-type bind<> calls — laying the bindings
// down with LuaBridge3 instead of sol2.
//
// Header-only consumption (welder::headers); the macro and backend come from
// <welder/rods/lua/luabridge/module.hpp> (which pulls in the Lua headers LuaBridge3
// needs). Build with welder_luabridge_add_module() so the output is a `require`-able
// `shapes_luabridge.so` (see this dir's CMakeLists.txt).
//
// [[=welder::doc]] annotations have no runtime home in Lua (no __doc__), but they
// are kept in the source: they are the material the LuaCATS (---@meta) stub emitter
// surfaces to the Lua language server.
#include <cstdint>

#include <welder/vocabulary.hpp>

#include <welder/rods/lua/luabridge/module.hpp>

// The namespace name doubles as the module name (require "shapes_luabridge").
namespace
[[=welder::doc("A small shapes module built by welder, for Lua (LuaBridge3).")]]
shapes_luabridge {

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

} // namespace shapes_luabridge

// One line emits luaopen_shapes_luabridge and binds the whole namespace into the
// module. The trailing block is optional post-glue: welder has already bound the
// namespace; here we add a hand-written field with LuaBridge3 directly (the module
// handle `module` names the built namespace, so we re-open it to register onto it).
WELDER_MODULE(shapes_luabridge, luabridge) {
    ::luabridge::getGlobalNamespace(module.L)
        .beginNamespace(module.path.front().c_str())
        .addVariable("BUILT_BY", "welder")
        .endNamespace();
}