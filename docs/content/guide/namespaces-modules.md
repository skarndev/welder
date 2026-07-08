# Namespaces & modules

Binding types one at a time is fine, but welder can also bind a **whole namespace**
— or emit an **entire importable module** — from a single declaration.

## Binding a namespace

`welder::welder<Rod>::weld_namespace<^^ns>(m)` walks a namespace and binds its
contents in **declaration order**:

- classes (via `weld_type<T>`),
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
        welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^geometry>(m);
    }
    ```

=== "Lua"

    ```cpp
    extern "C" int luaopen_geometry(lua_State* L) {
        sol::state_view lua(L);
        sol::table m = lua.create_table();
        welder::welder<welder::rods::sol2::rod>::weld_namespace<^^geometry>(m);
        return sol::stack::push(L, m);
    }
    ```

### Namespace variables

A namespace-scope variable binds as a module attribute:

- **const / constexpr** → a *value snapshot*;
- otherwise → a **live get/set** over the C++ global: a read returns the current
  value and a write flows back to the global. The **Python** rods implement it with
  a `ModuleType` `__class__` swap; the **sol2** rod with a metatable proxy
  (`__index`/`__newindex`) on the module table.

### Nested namespaces

A **nested** namespace resolves under the *parent's* policy (it has no `weld` of its
own): `automatic` recurses unless excluded; `opt_in` recurses only if included —
which keeps `detail` / `impl` namespaces out by giving the parent `opt_in`. A nested
namespace becomes a **submodule** when it holds bound content.

## Binding a single function or variable

Welding a whole namespace is convenient, but the **semi-manual** route lets you drop
down and bind one hand-picked free function or global directly onto a module handle —
the free-standing analogue of `weld_type<T>`, for when you want welder to lay down
just that entity and keep the rest of the entry point hand-written:

- `weld_function<^^fn>(m)` binds a single free function `fn`.
- `weld_variable<^^var>(m)` binds a single global/constant `var`.

The entity still needs its `[[=welder::weld(...)]]` (that is always the gate), and
its signature/type still runs the [bindability gate](bindability.md) — but you do not
have to weld, or even name, the enclosing namespace.

```cpp
namespace geometry {
[[=welder::weld(welder::lang::py)]] double distance(const Point& a, const Point& b);
[[=welder::weld(welder::lang::py)]] inline constexpr double kUnit{1.0};
}  // namespace geometry

PYBIND11_MODULE(geometry, m) {
    using weld = welder::welder<welder::rods::pybind11::rod<>>;
    weld::weld_function<^^geometry::distance>(m);  // one free function
    weld::weld_variable<^^geometry::kUnit>(m);     // one constant
    // … the rest of the module stays ordinary hand-written pybind11 code …
}
```

`weld_variable` follows the same const-vs-mutable rule as [namespace
variables](#namespace-variables) above (a value snapshot, or a live property on the
Python rods). Like `weld_type`, both take an optional trailing **name** argument used
verbatim — and it takes precedence over any
[`weld_as`](naming.md#weld_as-force-a-name-verbatim) on the entity:

```cpp
weld::weld_function<^^geometry::distance>(m, "dist");  // exposed as m.dist
weld::weld_variable<^^geometry::kUnit>(m, "UNIT");     // exposed as m.UNIT
```

An **overloaded** free function must be welded through its namespace (or by
reflecting the specific overload): `^^fn` on an overload set is ambiguous.

## Tack welding: an unmarked library

Everything so far needs a `weld` marker. But sometimes you want to bind a
**third-party library that has no welder annotations** and that you can't edit. For
that, swap the **carriage** — the traversal engine `welder::welder` drives — from the
default *stitch-welding* carriage (bind where the markers direct) to the
**tack-welding** carriage, which binds *greedily*: every reflectable type, free
function and global participates, namespaces are recursed, and every public base is
flattened in — the missing `weld` markers are simply ignored.

The carriage is `welder::welder`'s third template argument:

```cpp
namespace thirdparty {            // no welder annotations anywhere
struct Vec2 { double x, y; double length() const; };
Vec2 midpoint(const Vec2&, const Vec2&);
inline constexpr int ABI_VERSION{3};
}  // namespace thirdparty

using tack = welder::welder<welder::rods::pybind11::rod<>,
                            welder::naming::none,
                            welder::tack_welding_carriage>;   // ← greedy carriage

PYBIND11_MODULE(thirdparty, m) {
    tack::weld_namespace<^^thirdparty>(m);   // binds Vec2, midpoint, ABI_VERSION …
}
```

!!! warning "Bindability is still enforced"

    Tack welding drops the *marker* requirement, **not** the [bindability
    gate](bindability.md). A greedily-bound entity whose type welder can't represent
    (e.g. a function returning an unbindable type) is still a hard compile error — you
    just can't `mark::exclude` it, since the header is unannotated. Vouch for such a
    type with a type-level [`trust_bindable<T>`](trust-casters.md), or point the tack
    at a narrower sub-namespace. Any `mark::exclude` that *does* happen to be present
    is still honored, so a partially-annotated header can still prune.

Both carriages ship as `welder::stitch_welding_carriage` (the default) and
`welder::tack_welding_carriage`; a custom traversal is a
`welder::detail::basic_carriage<Resolution>` with your own resolution policy.

!!! tip "Subclassing `welder::welder`"

    Each `weld_*` entry point is a one-line forward to the carriage (which owns the
    resolution and the gates). To go beyond the stock flow you can either inject a
    different carriage (above) or **derive** from `welder::welder<Rod, Style, Carriage>`
    — being all-static it isn't a runtime base, but a subclass reaches the bound
    `rod_type` / `name_style` / `carriage_type` and the entry points to assemble a
    bespoke routine (a curated subset of a namespace, welded and hand-written
    registrations interleaved) without re-implementing the traversal or the gates.

## Binding a whole module

`WELDER_MODULE(ns, rod)` emits the language's C entry symbol
(`PyInit_<name>` for Python, `luaopen_<name>` for Lua) and fills the module from the
namespace — no `PYBIND11_MODULE`, no hand-written `luaopen_`, no per-type `weld_type`
calls. The namespace token doubles as the module name, and the namespace `doc`
becomes the module docstring (where the language has one). Include the rod's
`module.hpp` (not just its `rod.hpp`) to pull the macro in.

The `rod` selector is the **rod name** (`pybind11`, `nanobind`, `sol2`), not
the language. Everything above `WELDER_MODULE` — the namespace and its annotations
— is identical; only the includes and the selector change:

=== "Python (pybind11)"

    ```cpp title="shapes.cpp"
    #include <welder/vocabulary.hpp>
    #include <pybind11/pybind11.h>
    #include <pybind11/stl.h>
    #include <welder/rods/python/pybind11/module.hpp>  // rod + WELDER_MODULE

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
    #include <welder/vocabulary.hpp>
    #include <sol/sol.hpp>
    #include <welder/rods/lua/sol2/module.hpp>  // rod + WELDER_MODULE

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

Under the hood, `WELDER_MODULE` wraps
`welder::welder<Rod>::weld_module<^^ns>(m, pre, post)`: a *pre* hook, then
`weld_namespace`, then a *post* hook (your trailing block).

!!! warning "One `WELDER_MODULE` per rod per TU — but several rods can coexist"

    Two rods that emit the *same* entry symbol collide — pybind11 and nanobind
    both emit `PyInit_<name>`, so only one Python rod per TU. But a Python and a
    Lua `WELDER_MODULE` emit *different* symbols (`PyInit_shapes` vs
    `luaopen_shapes`), so one shared object can carry both. That's the basis for
    [shipping the same module across rods](../backends/multiple.md).

Next: [Docstrings](docstrings.md).
