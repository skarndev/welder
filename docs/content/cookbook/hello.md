# 01 — One of everything

*Source: [`examples/cookbook/01-hello`][src].*

welder welds four kinds of entity individually: a **type**, an **enum**, a **free
function** and a **namespace variable**. This recipe binds one of each with its own
`weld_*` call — the *semi-manual* route, where you keep control of the module
layout and welder does the members. (Recipe [02](discovery.md) hands the whole
namespace over instead.)

## The annotated C++

```cpp
namespace hello {

struct
[[=welder::weld(welder::lang::py), =welder::doc("A 2-D vector.")]]
Vec2 {
    double x{0.0};
    double y{0.0};

    [[=welder::doc("The Euclidean length.")]]
    double length() const { return std::sqrt(x * x + y * y); }

    Vec2 operator+(const Vec2& o) const { return Vec2{x + o.x, y + o.y}; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};

enum class
[[=welder::weld(welder::lang::py), =welder::doc("A primary color.")]]
Color { Red, Green, Blue };

[[
  =welder::weld(welder::lang::py),
  =welder::doc("The midpoint of two vectors."),
  =welder::returns("the point halfway between a and b")
]]
Vec2 midpoint(
    [[=welder::doc("one endpoint")]] const Vec2& a,
    [[=welder::doc("the other endpoint")]] const Vec2& b);

[[=welder::weld(welder::lang::py), =welder::doc("Circle constant.")]]
inline constexpr double TAU{6.283185307179586};

[[=welder::weld(welder::lang::py), =welder::doc("How many midpoints were taken.")]]
inline int midpoint_count{0};

} // namespace hello
```

## The binding TU

One `weld_*` call per entity, all through the one entry point:

```cpp
PYBIND11_MODULE(hello, m) {
    using weld = welder::welder<welder::rods::pybind11::rod<>>;
    weld::weld_type<hello::Vec2>(m);
    weld::weld_type<hello::Color>(m); // weld_type dispatches enums to the enum path
    weld::weld_function<^^hello::midpoint>(m);
    weld::weld_variable<^^hello::TAU>(m);
    weld::weld_variable<^^hello::midpoint_count>(m);
}
```

## What the check asserts

- `Vec2` is a baseless aggregate whose fields all bind, so welder synthesizes the
  positional constructor — `Vec2(3.0, 4.0)` works ([Binding a type](../guide/binding-types.md)).
- The member operators arrive as `__add__` / `__eq__`.
- `Color` is a real `enum.IntEnum`; scoped values stay `Color.Green`
  ([Enums](../guide/enums.md)).
- `midpoint(a=..., b=...)` — parameter names became keyword arguments, and
  `doc`/`returns` render a Google-style docstring
  ([Docstrings](../guide/docstrings.md)).
- `TAU` is a value snapshot; the *mutable* `midpoint_count` is a **live property**
  — C++ mutations are visible from Python and Python assignments reach the C++
  global ([Namespaces & modules](../guide/namespaces-modules.md#namespace-variables)).

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/01-hello