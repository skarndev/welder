# The Lua backend (sol2)

welder binds the *same* annotated C++ to Lua as to Python — you add
`welder::lang::lua` to a type's `weld` and register with the **sol2** backend. The
core (which members bind, inheritance, the bindability gate, namespaces) is shared
verbatim; only the emission differs. The result is a **loadable Lua C module**: a
shared object Lua's `require` finds on `package.cpath` and enters through
`luaopen_<name>`.

```cpp title="shapes_lua.cpp"
#include <welder/welder.hpp>                       // vocabulary (header-only)

#include <sol/sol.hpp>
#include <welder/backends/lua/sol2/backend.hpp>

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

// Emits `luaopen_shapes_lua` and binds the whole `shapes_lua` namespace... or,
// per type, write the entry yourself:
extern "C" int luaopen_shapes(lua_State* L) {
    sol::state_view lua(L);
    sol::table m = lua.create_table();
    welder::sol2::bind<Rect>(m);       // one type
    // welder::sol2::bind_namespace<^^ns>(m);  // or a whole namespace
    return sol::stack::push(L, m);     // return the module table
}
```

```lua
local s = require("shapes")
local r = s.Rect(3.0, 4.0)     -- or s.Rect.new(3.0, 4.0)
print(r:area())                -- 12.0  (methods use `:`)
print((r + s.Rect(1, 1)).w)    -- 4.0   (operator+ -> __add)
```

Or skip the boilerplate with the backend-agnostic entry macro, which binds a whole
namespace and emits the `luaopen_` symbol for you:

```cpp
WELDER_MODULE(shapes_lua, sol2) {
    module["BUILT_BY"] = "welder";   // optional hand-written glue
}
```

## Building a loadable module

Use `welder_sol2_add_module()` — the Lua analogue of `pybind11_add_module` — which
produces a `require`-able `<name>.so` with the right link model (the module resolves
`lua_*` from the host interpreter; it never bundles its own Lua):

```cmake
find_package(sol2 REQUIRED)
find_package(lua REQUIRED)
welder_sol2_add_module(shapes example.cpp)
target_link_libraries(shapes PRIVATE welder::headers)
```

!!! note "Header-only consumption only"

    A Lua binding TU consumes welder **header-only** (`#include
    <welder/welder.hpp>`), not `import welder;`: sol2's `<luaconf.h>` does not
    survive C++20 module dependency scanning. `welder_sol2_add_module` disables the
    scan for you.

## How Lua differs from Python

Everything welder resolves (policy, `mark::exclude/include`, inheritance, the
bindability gate) works identically; the target-language surface is what changes.

**Operators become Lua metamethods**, a smaller and asymmetric set:

| C++ | Lua | | C++ | Lua |
|---|---|---|---|---|
| `a + b` | `__add` | | `a == b` | `__eq` |
| `a - b` / `-a` | `__sub` / `__unm` | | `a < b` | `__lt` |
| `a * b` | `__mul` | | `a <= b` | `__le` |
| `a / b` | `__div` | | `a[i]` | `__index` |
| `a % b` | `__mod` | | `a(...)` | `__call` |

`operator!=`, `operator>` and `operator>=` map to **nothing** — Lua derives `~=`,
`>` and `>=` from `__eq`, `__lt` and `__le`, so they just work once those are bound.
C++ `operator^` is bitwise-xor → `__bxor` (not `__pow`); the bitwise metamethods
require Lua ≥ 5.3.

**Enums are tables.** Lua has no enum type, so a welded enum binds as a name→value
table (`Color.Red`); an unscoped enum's names are also mirrored onto the enclosing
module, mirroring C++.

**No runtime docstrings.** Lua has no `__doc__`, so `doc`/`returns` annotations are
ignored *at runtime*. Their home is a generated **LuaCATS stub** — see
[Stubs](#stubs-luacats) below.

**Namespace variables snapshot** at load time; **overloaded methods** collapse to
the last one bound (sol2 stores one value per name). Both are documented limits with
planned enhancements.

sol2 supports **multiple and virtual base classes**, so a multi-base diamond binds
here (as with pybind11; nanobind is single-inheritance only).

## Stubs (LuaCATS)

Because Lua drops docstrings at runtime, welder can emit a **LuaCATS
(`---@meta`) definition file** — the Lua analogue of the Python
[`.pyi` stubs](docstrings.md#stubs) — so the [Lua language
server](https://luals.github.io/) gives you completion, type hints and the
docstrings in your editor. Unlike the Python stubs (scraped from the *loaded*
module), a Lua stub is **reflection-emitted at build time** by the `welder::luacats`
backend, which walks the same welded types through the same core driver as sol2 and
writes LuaCATS text.

Write a tiny generator TU and let the entry macro provide `main()`:

```cpp title="shapes_stub.cpp"
#include <welder/welder.hpp>
#include <welder/backends/lua/luacats/stub.hpp>

WELDER_LUACATS_MAIN(shapes)   // emit the ---@meta stub for namespace ^^shapes
```

Wire it in CMake — it needs no sol2 or Lua, just the reflecting compiler:

```cmake
welder_luacats_generate_stub(shapes_stub
  SOURCES shapes_stub.cpp
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/shapes.lua)
```

The `Rect` above yields:

```lua title="shapes.lua"
---@meta

shapes = {}

---@class shapes.Rect
---@field w number
---@field h number
---@operator add(shapes.Rect): shapes.Rect
shapes.Rect = {}

---@return shapes.Rect
function shapes.Rect.new() end

---@param width number
---@param height number
---@return shapes.Rect
function shapes.Rect.new(width, height) end

---@return number
function shapes.Rect:area() end
```

Any `doc`/`returns`/parameter docs you wrote land in the LuaCATS `---`
comments and `@field`/`@param`/`@return` tags. The C++→LuaCATS **type map** covers
scalars (`integer`/`number`/`boolean`/`string`), the STL wrappers welder recurses
(`std::vector<T>` → `T[]`, `std::map<K,V>` → `table<K,V>`, `std::optional<T>` →
`T?`, smart pointers → the pointee) and welded classes/enums (their dotted name);
anything else degrades to `any`. Enums become `---@enum` tables, welded bases become
`---@class X : Base`. Current limits: overloaded methods/constructors emit repeated
`function` definitions rather than idiomatic `---@overload`, and const members are
not yet marked read-only.
