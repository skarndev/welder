# Enums

A welded enum — scoped or unscoped — binds via the same `bind<T>` entry point
(dispatched to the enum path by `is_enum_v`). Each **enumerator resolves like a
data member**: the enum's `policy` plus per-enumerator `exclude`/`include` marks
decide what binds. What it becomes in the target language differs:

- **Python** — a stdlib `enum.IntEnum` (pybind11 `py::native_enum`, nanobind
  `nb::is_arithmetic`; int-convertible).
- **Lua** — a plain `name → value` **table** (Lua has no enum type).

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

=== "Python"

    ```pycon
    >>> Direction.West.value     # an enum.IntEnum member
    3
    >>> hasattr(Direction, "South")
    False
    ```

=== "Lua"

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

=== "Python"

    ```pycon
    >>> Signal.Red, Red          # both work
    (<Signal.Red: 2>, <Signal.Red: 2>)
    ```

=== "Lua"

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

## Docs

The enum's `doc` becomes the Python docstring. welder doesn't currently surface
per-enumerator docs at runtime (Lua has no docstring slot either). On the
**C++** side the [Doxygen filter](cpp-docs.md) surfaces enumerator docs, and the
[LuaCATS stub](../backends/lua.md#stubs-luacats) marks the table `---@enum`.

Next: [Inheritance](inheritance.md).
