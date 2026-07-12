# Lua (LuaBridge3)

welder binds Lua through **two** rods. The [sol2 page](lua.md) covers the first;
this is the second — the **LuaBridge3** rod. It binds the *same* annotated C++ (you
still just add `welder::lang::lua` to a type's `weld`), runs the *same* tests, and
produces the same kind of **loadable Lua C module** entered through
`luaopen_<name>`. Only the framework laying the bindings down differs.

Why a second Lua rod? [LuaBridge3](https://github.com/kunitoki/LuaBridge3) is a
lightweight, dependency-free, single-header binding library that supports a wider
range of Lua runtimes than sol2 — **PUC-Lua 5.1–5.5, LuaJIT, Luau, Ravi** — so it is
the rod to reach for when you need a newer Lua (sol2/3.5.0 caps at 5.4), a different
license, or a smaller dependency.

```cpp title="shapes_luabridge.cpp"
#include <welder/vocabulary.hpp>                       // vocabulary (header-only)

#include <welder/rods/lua/luabridge/rod.hpp>           // pulls in <lua.hpp> + LuaBridge3

struct
[[=welder::weld(welder::lang::lua)]]
Rect {
    double w{0.0};
    double h{0.0};
    Rect() = default;
    Rect(double width, double height) : w{width}, h{height} {}
    double area() const { return w * h; }
    Rect operator+(const Rect& o) const { return {w + o.w, h + o.h}; } // __add
};

// Per type, write the entry yourself. LuaBridge3 registers into a named namespace
// under _G, so welder builds the module there, returns the table, and clears _G.
extern "C" int luaopen_shapes(lua_State* L) {
    using rod = welder::rods::luabridge::rod;
    rod::module_type m{L, {"shapes"}};                 // {lua_State*, module path}
    welder::welder<rod>::weld_type<Rect>(m);           // one type
    // welder::welder<rod>::weld_namespace<^^ns>(m);   // or a whole namespace
    lua_getglobal(L, "shapes");                        // the populated table
    lua_pushnil(L); lua_setglobal(L, "shapes");        // keep _G clean
    return 1;
}
```

```lua
local s = require("shapes")
local r = s.Rect(3.0, 4.0)     -- or s.Rect.new(3.0, 4.0)
print(r:area())                -- 12.0  (methods use `:`)
print((r + s.Rect(1, 1)).w)    -- 4.0   (operator+ -> __add)
```

Or skip the boilerplate with the rod-agnostic
[entry macro](../guide/namespaces-modules.md#binding-a-whole-module) (from
`welder/rods/lua/luabridge/module.hpp`), which binds a whole namespace and emits the
`luaopen_` symbol — building under `_G[<name>]`, returning the table, and clearing
the global for you. The selector is the rod name **`luabridge`**:

```cpp
WELDER_MODULE(shapes_luabridge, luabridge) {
    // optional hand-written glue; `module` is the {lua_State*, path} handle
    ::luabridge::getGlobalNamespace(module.L)
        .beginNamespace(module.path.front().c_str())
        .addVariable("BUILT_BY", "welder")
        .endNamespace();
}
```

Only the module header include differs from sol2 (`luabridge/…` instead of
`sol2/…`) — the rod header includes `<lua.hpp>` and `<LuaBridge/LuaBridge.h>` for
you, so a binding TU needs just that one include.

## How it differs from sol2

Both Lua rods bind the same core surface — metamethods, enums-as-tables, constructors
exposed as both `T(…)` and `T.new(…)`, const/live namespace variables — asserted by
the *same* busted specs. Where they genuinely differ:

| | sol2 | LuaBridge3 |
|---|---|---|
| Module handle | a `sol::table` you create | a `{lua_State*, path}` under `_G` (returned, `_G` cleared) |
| Multiple inheritance | yes, incl. **virtual** diamonds | non-virtual only — **no virtual bases** |
| Namespace variables (mutable) | metatable proxy | native `addProperty` |
| Lua versions | ≤ 5.4 | 5.1–5.5, LuaJIT, Luau, Ravi |

The virtual-base limitation is the one behavioral gap: LuaBridge3 computes a base's
cast offset as plain pointer arithmetic, which a virtual base breaks, so a *virtual*
diamond that binds under sol2 does not under LuaBridge3 (welder still supports its
**non-virtual** multiple inheritance). Everything else — [enums](../guide/enums.md)
as name→value tables, [operators](lua.md#operators-become-metamethods) as metamethods
(`operator[]` rides LuaBridge3's index fallback so it coexists with member access),
[docstrings](../guide/docstrings.md) dropped at runtime and recovered in the
[LuaCATS stub](lua.md#stubs-luacats) — matches sol2.

## Building

LuaBridge3 has no Conan Center package and ships no CMake config, so welder sources
it two ways:

- **As a consumer** (you `#include` the rod in your own project): bring your own
  LuaBridge3 headers — welder finds them with `find_package(LuaBridge3)` or
  `-DWELDER_LUABRIDGE_DIR=<dir containing LuaBridge/LuaBridge.h>`.
- **Building welder's own tests/examples**: welder FetchContents a pinned LuaBridge3
  commit automatically (`-DWELDER_LUABRIDGE_FETCH=ON`, on by default when tests are
  enabled; pin with `WELDER_LUABRIDGE_GIT_TAG`).

Lua itself comes from your system/install, with the rod's **own** version knobs so it
can target a newer Lua than sol2 without disturbing it:

```bash
# Build the LuaBridge3 rod/tests against Lua 5.5 (sol2 stays on 5.4)
cmake --preset welder-gcc16 \
  -DWELDER_LUABRIDGE_LUA_DIR="$(brew --prefix lua)" \
  -DWELDER_LUABRIDGE_LUA_VERSION=5.5
```

`WELDER_LUABRIDGE_LUA_VERSION` / `WELDER_LUABRIDGE_LUA_DIR` default to the sol2 knobs
(`WELDER_LUA_VERSION` / `WELDER_LUA_DIR`), so a single Lua install serves both rods
unless you point them apart. Create an extension with
`welder_luabridge_add_module(<name> <sources>)` (the LuaBridge3 counterpart of
`welder_sol2_add_module`); the `<name>` must match the `luaopen_<name>` module name.

!!! note "ABI must match the interpreter"
    Like any loadable Lua module, a LuaBridge3 extension resolves `lua_*` from the
    host interpreter and has no cross-minor ABI — a 5.4 module in a 5.5 interpreter
    segfaults. welder hard-errors at configure time if the found Lua minor does not
    match `WELDER_LUABRIDGE_LUA_VERSION`.