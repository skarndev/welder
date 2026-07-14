# Enums

A welded enum — scoped or unscoped — binds via the same `weld_type<T>` entry point
(dispatched to the enum path by `is_enum_v`). Each **enumerator resolves like a
data member**: the enum's `policy` plus per-enumerator `exclude`/`include` marks
decide what binds. What it becomes in the target language differs:

- **Python** — a stdlib `enum.IntEnum` (pybind11 `py::native_enum`, nanobind
  `nb::is_arithmetic`; int-convertible).
- **Lua** — a plain `name → value` **table** (Lua has no enum type).

!!! example "In the cookbook"

    [Recipe 01 — One of everything](../cookbook/hello.md) welds a scoped enum and
    asserts the `IntEnum` surface from Python.

!!! warning "Grammar: the annotation goes *after* the enumerator name"

    Unlike a struct member (whose annotation precedes it), a C++ enumerator's
    annotation comes **after** its name:

    ```cpp
    South [[=welder::mark::exclude]],   // enumerator
    ```

## Scoped enum, automatic policy

```cpp
enum class
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Direction {
    North,
    East,
    South [[=welder::mark::exclude]],   // excluded → not bound
    West                                // keeps its C++ value (3)
};
```

Excluding an enumerator does **not** renumber the rest — `West` is still `3`. The
enumerator stays qualified under the enum name:

=== ":simple-python: Python"

    ```pycon
    >>> Direction.West.value     # an enum.IntEnum member
    3
    >>> hasattr(Direction, "South")
    False
    ```

=== ":simple-lua: Lua"

    ```lua
    print(Direction.West)        --> 3    (table maps name → value directly)
    print(Direction.South)       --> nil  (excluded)
    ```

## Unscoped enum — values exported

An unscoped enum also mirrors its enumerators onto the enclosing module unqualified
(pybind11 `export_values()`; the sol2 rod copies the names onto the module
table), matching C++:

```cpp
enum [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Signal { Green, Yellow, Red };
```

=== ":simple-python: Python"

    ```pycon
    >>> Signal.Red, Red          # both work
    (<Signal.Red: 2>, <Signal.Red: 2>)
    ```

=== ":simple-lua: Lua"

    ```lua
    print(Signal.Red, Red)       --> 2  2
    ```

## Scoped enum, opt-in policy

```cpp
enum class
[[=welder::weld(welder::lang::py, welder::lang::lua), =welder::policy::opt_in]]
Level {
    Debug [[=welder::mark::include]],
    Info  [[=welder::mark::include]],
    Trace                               // not opted in → not bound
};
```

## Enum-typed members

An enum-typed member or parameter binds because the enum is welded — bind the enum
**first** (like a welded base), then the type that uses it:

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Compass {
    Direction facing;
};
```

When you [bind a whole namespace](namespaces-modules.md), declaration order handles
this for you — put the enums before the structs that use them.

!!! note "Per-enumerator docs are omitted at runtime"

    A `doc` on an individual **enumerator** does not reach the generated runtime
    bindings: neither Python rod has a per-member docstring slot to fill
    (pybind11/nanobind expose none for enum members), and Lua has no runtime
    docstring at all. The text is not lost — the [Doxygen filter](cpp-docs.md)
    surfaces enumerator docs in the C++ reference, and the
    [LuaCATS stub](../backends/lua.md#stubs-luacats) documents the bound table
    (marked `---@enum`). How docs flow in general is covered later, in
    [Docstrings](docstrings.md).

Next: [Inheritance](inheritance.md).
