# Namespaces & modules

Binding types one at a time is fine, but welder can also bind a **whole namespace**
— or emit an **entire importable module** — from a single declaration.

## Binding a namespace

`welder::<backend>::bind_namespace<^^ns>(m)` walks a namespace and binds its
contents in **declaration order**:

- classes (via `bind<T>`),
- free functions (overloads included),
- namespace-scope variables.

`weld` gates *leaf entities only* (a class type, a free function, a namespace-scope
variable — **namespaces are never welded**). The namespace's `policy` (default
`automatic`) plus member marks then resolve what actually binds.

```cpp
namespace geometry {

struct [[=welder::weld(welder::lang::py, welder::lang::lua)]] Point { double x, y; };

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
double distance(const Point& a, const Point& b) { /* … */ }

}  // namespace geometry
```

=== "Python"

    ```cpp
    PYBIND11_MODULE(geometry, m) {
        welder::pybind11::bind_namespace<^^geometry>(m);
    }
    ```

=== "Lua"

    ```cpp
    extern "C" int luaopen_geometry(lua_State* L) {
        sol::state_view lua(L);
        sol::table m = lua.create_table();
        welder::sol2::bind_namespace<^^geometry>(m);
        return sol::stack::push(L, m);
    }
    ```

### Namespace variables

A namespace-scope variable binds as a module attribute:

- **const / constexpr** → a *value snapshot*;
- otherwise → on the **Python** backends, a **live get/set property** over the C++
  global (via a `ModuleType` `__class__` swap). The **sol2** backend snapshots the
  value at load time (a live get/set property is planned).

### Nested namespaces

A **nested** namespace resolves under the *parent's* policy (it has no `weld` of its
own): `automatic` recurses unless excluded; `opt_in` recurses only if included —
which keeps `detail` / `impl` namespaces out by giving the parent `opt_in`. A nested
namespace becomes a **submodule** when it holds bound content.

## Binding a whole module

`WELDER_MODULE(ns, backend)` emits the language's C entry symbol
(`PyInit_<name>` for Python, `luaopen_<name>` for Lua) and fills the module from the
namespace — no `PYBIND11_MODULE`, no hand-written `luaopen_`, no per-type `bind`
calls. The namespace token doubles as the module name, and the namespace `doc`
becomes the module docstring (where the language has one).

The `backend` selector is the **backend name** (`pybind11`, `nanobind`, `sol2`), not
the language. Everything above `WELDER_MODULE` — the namespace and its annotations
— is identical; only the includes and the selector change:

=== "Python (pybind11)"

    ```cpp title="shapes.cpp"
    #include <welder/welder.hpp>
    #include <pybind11/pybind11.h>
    #include <pybind11/stl.h>
    #include <welder/backends/python/pybind11/backend.hpp>

    namespace
    [[=welder::doc("A small shapes module built by welder.")]]
    shapes {

    struct
    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("An axis-aligned rectangle.")]]
    Rect {
        double w{0.0}, h{0.0};
        Rect() = default;
        Rect(double width, double height) : w{width}, h{height} {}

        [[=welder::doc("The area of the rectangle.")]]
        double area() const { return w * h; }
    };

    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("Scale a length by a factor.")]]
    double scale(
        [[=welder::doc("the length to scale")]] double length,
        [[=welder::doc("the multiplier")]] double factor) {
        return length * factor;
    }

    }  // namespace shapes

    // One line: emits PyInit_shapes and binds the whole namespace into `module`.
    // The trailing block is optional post-glue (the module handle is in scope).
    WELDER_MODULE(shapes, pybind11) {
        module.attr("VERSION") = "1.0";
    }
    ```

    ```pycon
    >>> import shapes
    >>> shapes.__doc__
    'A small shapes module built by welder.'
    >>> shapes.Rect(2.0, 3.0).area()
    6.0
    >>> shapes.scale(length=10.0, factor=2.5)
    25.0
    >>> shapes.VERSION
    '1.0'
    ```

=== "Lua (sol2)"

    ```cpp title="shapes_lua.cpp"
    #include <welder/welder.hpp>
    #include <sol/sol.hpp>
    #include <welder/backends/lua/sol2/backend.hpp>

    // ... namespace shapes { Rect, scale } exactly as in the Python tab ...

    // Emits luaopen_shapes and binds the whole namespace into `module`
    // (a sol::table here). The doc annotations are ignored at runtime by sol2 —
    // their Lua home is the LuaCATS stub.
    WELDER_MODULE(shapes, sol2) {
        module["VERSION"] = "1.0";
    }
    ```

    ```lua
    local shapes = require("shapes")
    print(shapes.Rect(2.0, 3.0):area())     --> 6.0
    print(shapes.scale(10.0, 2.5))          --> 25.0  (no keyword args in Lua)
    print(shapes.VERSION)                   --> 1.0
    ```

Under the hood, `WELDER_MODULE` wraps `build_module<^^ns>(m, pre, post)`: a *pre*
hook, then `bind_namespace`, then a *post* hook (your trailing block).

!!! warning "One `WELDER_MODULE` per backend per TU — but several backends can coexist"

    Two backends that emit the *same* entry symbol collide — pybind11 and nanobind
    both emit `PyInit_<name>`, so only one Python backend per TU. But a Python and a
    Lua `WELDER_MODULE` emit *different* symbols (`PyInit_shapes` vs
    `luaopen_shapes`), so one shared object can carry both. That's the basis for
    [shipping the same module across backends](../backends/multiple.md).

Next: [Docstrings](docstrings.md).
