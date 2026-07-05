# Binding a type

Every backend exposes the same entry point — `welder::<backend>::bind<T>(m)`
(`welder::pybind11::bind`, `welder::nanobind::bind`, `welder::sol2::bind`) — which
reflects `T` and emits its whole surface. This page covers what "whole surface"
means for a class: data members, constructors, methods, and operators. **The
annotations and the resolution are identical across backends;** only the emitted
target-language surface differs. Each member obeys the
[resolution rule](annotations.md#the-resolution-rule) — excludes, includes, and the
type's policy decide what participates.

The examples below weld one struct and show how it looks from each language. The
C++ is the same; pick your tab.

## Data members

Public data members bind as read/write attributes.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Point {
    double x{0.0};
    double y{0.0};
};
```

=== "Python"

    ```pycon
    >>> p = Point(); p.x = 3.0; p.y = 4.0
    >>> p.x, p.y
    (3.0, 4.0)
    ```

=== "Lua"

    ```lua
    local p = Point(); p.x = 3.0; p.y = 4.0
    print(p.x, p.y)   --> 3.0  4.0
    ```

Every bound member's type must pass the [bindability gate](bindability.md) — if the
backend can't convert it to a meaningful value in the target language, you get a
compile error naming the type, never a silent skip.

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
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Rect {                 // an aggregate: no user ctors, no bases
    double w{0.0};
    double h{0.0};
};
```

=== "Python"

    ```pycon
    >>> Rect(2.0, 3.0).w      # synthesized field constructor
    2.0
    ```

=== "Lua"

    ```lua
    print(Rect(2.0, 3.0).w)   --> 2.0   (synthesized field constructor)
    ```

Compare with a type that declares its own constructors — each public one binds:

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Rect {
    double w{0.0};
    double h{0.0};

    Rect() = default;
    Rect(double width, double height) : w{width}, h{height} {}
};
```

### Parameter names → keyword arguments (Python)

When **every** parameter of a signature is named, welder passes the names through
as `py::arg`, so they work as Python keyword arguments:

```pycon
>>> Rect(width=2.0, height=3.0).area()
6.0
```

Lua has no keyword arguments, so this is a Python-only convenience; the same
constructor is still callable positionally there.

## Methods and static methods

Member functions bind as methods; `static` member functions as static/free
functions on the type. Overloads are all registered on every backend — the Python
backends (pybind11/nanobind) chain them, and the **sol2** backend groups a name's
overloads into one `sol::overload(…)` — so each overload dispatches on its arguments
at call time.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Rect {
    double w{0.0}, h{0.0};

    [[=welder::doc("The area of the rectangle.")]]
    double area() const { return w * h; }

    static Rect square(double s) { return Rect{s, s}; }
};
```

=== "Python"

    ```pycon
    >>> Rect(2.0, 3.0).area()
    6.0
    >>> Rect.square(5.0).area()
    25.0
    ```

=== "Lua"

    ```lua
    print(Rect(2.0, 3.0):area())     --> 6.0   (method call uses `:`)
    print(Rect.square(5.0):area())   --> 25.0  (static uses `.`)
    ```

## Overloaded operators

A **member** operator binds under the target language's special method /
metamethod, told apart unary vs. binary by arity. The mapping differs per language:

=== "Python"

    | C++ | Python | C++ | Python |
    |---|---|---|---|
    | `operator+` | `__add__` | `operator==` | `__eq__` |
    | `operator-` (binary) | `__sub__` | `operator-` (unary) | `__neg__` |
    | `operator*` | `__mul__` | `operator[]` | `__getitem__` |
    | `operator()` | `__call__` | `operator<` | `__lt__` |

    Arithmetic, bitwise, comparison, call and subscript operators are covered.

=== "Lua"

    | C++ | Lua | C++ | Lua |
    |---|---|---|---|
    | `operator+` | `__add` | `operator==` | `__eq` |
    | `operator-` (binary) | `__sub` | `operator-` (unary) | `__unm` |
    | `operator*` | `__mul` | `operator[]` | `__index` |
    | `operator()` | `__call` | `operator<` | `__lt` |

    Lua's metamethod set is smaller and asymmetric — `!=`, `>`, `>=` are *derived*
    from `__eq`, `__lt`, `__le`, so you don't bind them. See the
    [Lua backend page](../backends/lua.md#operators-become-metamethods) for the full
    table.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Vec2 {
    double x{0.0}, y{0.0};
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-() const { return {-x, -y}; }        // unary → __neg__ / __unm
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
};
```

!!! info "Deliberately not mapped"

    In-place compound assignment (`operator+=`) is **not** mapped — Python falls
    back to `a = a + b` via `__add__`. Nor are `<=>`, `&&`, `||`, `++`, `--`, or
    `operator=` (a special member). **Free** (non-member) operators aren't bound
    yet.

Next: [Enums](enums.md).
