# Namespaces & modules

Binding types one at a time is fine, but welder can also bind a **whole namespace**
— or emit an **entire importable module** — from a single declaration.

## Binding a namespace

`welder::pybind11::bind_namespace<^^ns>(m)` walks a namespace and binds its
contents in **declaration order**:

- classes (via `bind<T>`),
- free functions (overloads included),
- namespace-scope variables.

`weld` gates *leaf entities only* (a class type, a free function, a namespace-scope
variable — **namespaces are never welded**). The namespace's `policy` (default
`automatic`) plus member marks then resolve what actually binds.

```cpp
namespace geometry {

struct [[=welder::weld(welder::lang::py)]] Point { double x, y; };

[[=welder::weld(welder::lang::py)]]
double distance(const Point& a, const Point& b) { /* … */ }

}  // namespace geometry

PYBIND11_MODULE(geometry, m) {
    welder::pybind11::bind_namespace<^^geometry>(m);
}
```

### Namespace variables

A namespace-scope variable binds as a module attribute:

- **const / constexpr** → a *value snapshot*;
- otherwise → a **live get/set property** over the C++ global (via a `ModuleType`
  `__class__` swap).

### Nested namespaces

A **nested** namespace resolves under the *parent's* policy (it has no `weld` of its
own): `automatic` recurses unless excluded; `opt_in` recurses only if included —
which keeps `detail` / `impl` namespaces out by giving the parent `opt_in`. A nested
namespace becomes a **submodule** when it holds bound content.

## Binding a whole module

`WELDER_MODULE(ns, backend)` emits the C entry symbol (`PyInit_<name>`) and fills
the module from the namespace — no `PYBIND11_MODULE`, no per-type `bind` calls. The
namespace token doubles as the module name, and the namespace `doc` becomes the
module docstring.

```cpp title="shapes.cpp"
#include <welder/welder.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/backends/pybind11.hpp>

namespace
[[=welder::doc("A small shapes module built by welder.")]]
shapes {

struct
[[=welder::weld(welder::lang::py), =welder::doc("An axis-aligned rectangle.")]]
Rect {
    double w{0.0}, h{0.0};
    Rect() = default;
    Rect(double width, double height) : w{width}, h{height} {}

    [[=welder::doc("The area of the rectangle.")]]
    double area() const { return w * h; }
};

[[=welder::weld(welder::lang::py), =welder::doc("Scale a length by a factor.")]]
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

Under the hood, `WELDER_MODULE` wraps `build_module<^^ns>(m, pre, post)`: a *pre*
hook, then `bind_namespace`, then a *post* hook (your trailing block).

!!! warning "One `WELDER_MODULE` per backend per TU"

    Two Python backends in one translation unit would both emit
    `PyInit_<name>` and collide.

Next: [Docstrings](docstrings.md).
