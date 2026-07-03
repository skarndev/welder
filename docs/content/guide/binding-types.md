# Binding a type

`welder::pybind11::bind<T>(m)` reflects `T` and emits its whole surface. This page
covers what "whole surface" means for a class: data members, constructors, methods,
and operators. Each obeys the [resolution rule](annotations.md#the-resolution-rule)
— excludes, includes, and the type's policy decide what participates.

## Data members

Public data members bind as read/write attributes.

```cpp
struct [[=welder::weld(welder::lang::py)]]
Point {
    double x{0.0};
    double y{0.0};
};
```

```pycon
>>> p = Point(); p.x = 3.0; p.y = 4.0
>>> p.x, p.y
(3.0, 4.0)
```

Every bound member's type must pass the [bindability gate](bindability.md) — if
pybind11 can't convert it to a meaningful Python value, you get a compile error
naming the type, never a silent skip.

## Constructors

welder binds:

- the **default constructor**, if present;
- **each public, non-copy/non-move constructor** → `pybind11::init<…>`;
- for a **baseless aggregate**, a *synthesized field constructor* that brace-inits
  it — giving Python `T(f0, f1, …)`.

!!! note "Why aggregates are special"

    Aggregate initialization is positional and all-or-nothing, so the synthesized
    constructor is only offered when **every** field binds.

```cpp
struct [[=welder::weld(welder::lang::py)]]
Rect {                 // an aggregate: no user ctors, no bases
    double w{0.0};
    double h{0.0};
};
```

```pycon
>>> Rect(2.0, 3.0).w      # synthesized field constructor
2.0
```

Compare with a type that declares its own constructors — each public one binds:

```cpp
struct [[=welder::weld(welder::lang::py)]]
Rect {
    double w{0.0};
    double h{0.0};

    Rect() = default;
    Rect(double width, double height) : w{width}, h{height} {}
};
```

### Parameter names → keyword arguments

When **every** parameter of a signature is named, welder passes the names through
as `py::arg`, so they work as Python keyword arguments:

```pycon
>>> Rect(width=2.0, height=3.0).area()
6.0
```

## Methods and static methods

Member functions bind as methods; `static` member functions as static methods.
Overloads are all registered — pybind11 dispatches on the argument types.

```cpp
struct [[=welder::weld(welder::lang::py)]]
Rect {
    double w{0.0}, h{0.0};

    [[=welder::doc("The area of the rectangle.")]]
    double area() const { return w * h; }

    static Rect square(double s) { return Rect{s, s}; }
};
```

```pycon
>>> Rect(2.0, 3.0).area()
6.0
>>> Rect.square(5.0).area()
25.0
```

## Overloaded operators

A **member** operator binds under its Python special method, told apart unary vs.
binary by arity:

| C++ | Python | C++ | Python |
|---|---|---|---|
| `operator+` | `__add__` | `operator==` | `__eq__` |
| `operator-` (binary) | `__sub__` | `operator-` (unary) | `__neg__` |
| `operator*` | `__mul__` | `operator[]` | `__getitem__` |
| `operator()` | `__call__` | `operator<` | `__lt__` |

```cpp
struct [[=welder::weld(welder::lang::py)]]
Vec2 {
    double x{0.0}, y{0.0};
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-() const { return {-x, -y}; }        // unary → __neg__
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};
```

Arithmetic, bitwise, comparison, call and subscript operators are covered.

!!! info "Deliberately not mapped"

    In-place compound assignment (`operator+=`) is **not** mapped — Python falls
    back to `a = a + b` via `__add__`. Nor are `<=>`, `&&`, `||`, `++`, `--`, or
    `operator=` (a special member). **Free** (non-member) operators aren't bound
    yet.

Next: [Enums](enums.md).
