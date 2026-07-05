# Lua (sol2)

welder binds the *same* annotated C++ to Lua as to Python — you add
`welder::lang::lua` to a type's `weld` and register with the **sol2** backend. The
core (which members bind, inheritance, the [bindability
gate](../guide/bindability.md), namespaces) is shared verbatim; only the emission
differs. The result is a **loadable Lua C module**: a shared object Lua's `require`
finds on `package.cpath` and enters through `luaopen_<name>`.

Everything in the [guide](../guide/index.md) applies to Lua; this page is the
sol2-specific detail — how the entry point works, where Lua's surface differs from
Python's, and the LuaCATS stub that carries the docstrings Lua has no runtime slot
for.

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

// Per type, write the entry yourself:
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

Or skip the boilerplate with the backend-agnostic
[entry macro](../guide/namespaces-modules.md#binding-a-whole-module), which binds a
whole namespace and emits the `luaopen_` symbol for you. The selector is the backend
name **`sol2`**, not `lua`:

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
welder_sol2_add_module(shapes example.cpp)
target_compile_features(shapes PRIVATE cxx_std_26)
target_link_libraries(shapes PRIVATE welder::headers)
```

The target name must match the namespace token in `WELDER_MODULE(shapes, sol2)` so
`luaopen_shapes` is the loaded entry point.

!!! note "Header-only consumption only"

    A Lua binding TU consumes welder **header-only** (`#include
    <welder/welder.hpp>`), not `import welder;`: sol2's `<luaconf.h>` does not
    survive C++20 module dependency scanning. `welder_sol2_add_module` disables the
    scan for you (`CXX_SCAN_FOR_MODULES OFF`).

!!! warning "Match the Lua minor version"

    A loadable module has no cross-minor ABI compatibility — a Lua 5.4 module loaded
    by a 5.5 interpreter segfaults. sol2 supplies only the C++ headers; **Lua itself
    comes from your system/install**, steered by `-DWELDER_LUA_DIR` (an install
    prefix, e.g. `$(brew --prefix lua@5.4)`) and pinned with `-DWELDER_LUA_VERSION`
    (default `5.4`). A configure-time guard hard-errors on a minor mismatch. (sol2
    3.5.0 does not support Lua 5.5.)

## How Lua differs from Python

Everything welder resolves (policy, `mark::exclude/include`, inheritance, the
bindability gate) works identically; the target-language surface is what changes.

### Operators become metamethods

Lua's metamethod set is smaller and asymmetric. Every welded **member** operator
binds to a metamethod (told apart unary vs. binary by arity); this is the complete
set welder maps:

| C++ | Lua | | C++ | Lua |
|---|---|---|---|---|
| `a + b` | `__add` | | `a == b` | `__eq` |
| `a - b` | `__sub` | | `a < b` | `__lt` |
| `-a` (unary) | `__unm` | | `a <= b` | `__le` |
| `a * b` | `__mul` | | `a(...)` | `__call` |
| `a / b` | `__div` | | `a[i]` | `__index` |
| `a % b` | `__mod` | | | |

Bitwise operators map too, but only on **Lua ≥ 5.3** (not LuaJIT's 5.1 ABI), so
they are `#if`-gated:

| C++ | Lua | | C++ | Lua |
|---|---|---|---|---|
| `a ^ b` | `__bxor` | | `a << b` | `__shl` |
| `a & b` | `__band` | | `a >> b` | `__shr` |
| `a \| b` | `__bor` | | `~a` (unary) | `__bnot` |

`operator!=`, `operator>` and `operator>=` map to **nothing** — Lua derives `~=`,
`>` and `>=` from `__eq`, `__lt` and `__le`, so they just work once those are bound.
Note C++ `operator^` is bitwise-xor → `__bxor` (**not** `__pow`/power).

### Everything else

- **Enums are tables.** Lua has no enum type, so a welded enum binds as a name→value
  table (`Color.Red` *is* the value); an unscoped enum's names are also mirrored onto
  the enclosing module, mirroring C++.
- **No runtime docstrings.** Lua has no `__doc__`, so `doc`/`returns` annotations are
  ignored *at runtime*. Their home is the generated [LuaCATS stub](#stubs-luacats).
- **Namespace variables snapshot** at load time (a live get/set property is planned).
- **Overloaded methods, functions and operators** are grouped into a single
  `sol::overload(…)` (sol2 stores one value per name / metamethod slot), so every
  overload of a name dispatches at call time — `c:sum(a)` and `c:sum(a, b)` both work.
  A same-named method in a derived class still hides the base's, matching C++.
- **Multiple and virtual base classes** are supported (as with pybind11; nanobind is
  single-inheritance only), so a multi-base diamond binds here.

## Stubs (LuaCATS)

Because Lua drops docstrings at runtime, welder can emit a **LuaCATS
(`---@meta`) definition file** — the Lua analogue of the Python
[`.pyi` stubs](../guide/docstrings.md#stubs) — so the [Lua language
server](https://luals.github.io/) gives you completion, type hints and the
docstrings in your editor. Unlike the Python stubs (scraped from the *loaded*
module), a Lua stub is **reflection-emitted at build time** by the `welder::luacats`
backend, which walks the same welded types through the same core driver as sol2 and
writes LuaCATS text — so it needs no sol2 or Lua at all, just the reflecting
compiler.

Write a tiny generator TU and let the entry macro provide `main()`:

```cpp title="shapes_stub.cpp"
#include <welder/welder.hpp>
#include <welder/backends/lua/luacats/backend.hpp>

WELDER_LUACATS_MAIN(shapes)   // emit the ---@meta stub for namespace ^^shapes
```

Wire it in CMake:

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
`---@class X : Base`. Overloaded methods, constructors and free functions render as a
single documented `function` plus idiomatic `---@overload fun(…)` lines (the primary
signature keeps its full `@param`/summary docs; the extra signatures carry types
only, which is all LuaCATS `---@overload` records). A `const` data member is noted
`(read-only)` in its `---@field` description — LuaCATS has no read-only field tag
([an open lua-language-server request](https://github.com/LuaLS/lua-language-server/discussions/2379)),
so the immutability the sol2 runtime enforces is documented here, not machine-checked.

Not every runtime metamethod has a stub form. LuaCATS `---@operator` only names the
operators the language server models — the arithmetic and bitwise ones plus
`call`/`len`/`concat`/`unm` — so the **comparison** (`==`, `<`, `<=`) and
**subscript** (`[]`) metamethods the sol2 runtime binds are *omitted* from the stub:
lua-language-server has no `---@operator` spelling for them (`==` is always allowed
and yields a boolean; indexing is expressed with `---@field [key] value`). They work
at runtime regardless; the stub simply can't type them.

welder's own test suite runs the emitted stub through `lua-language-server --check`
when the server is installed (the Lua analogue of type-checking the `.pyi` stubs with
mypy), so a malformed annotation or dangling type reference fails the build.
