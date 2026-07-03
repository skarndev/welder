# Enums

A welded enum — scoped or unscoped — binds via the same `bind<T>` entry point
(dispatched to the enum path by `is_enum_v`) as a `py::enum_`. Each **enumerator
resolves like a data member**: the enum's `policy` plus per-enumerator
`exclude`/`include` marks decide what binds.

!!! warning "Grammar: the annotation goes *after* the enumerator name"

    Unlike a struct member (whose annotation precedes it), a C++ enumerator's
    annotation comes **after** its name:

    ```cpp
    South [[=welder::mark::exclude]],   // enumerator
    ```

## Scoped enum, automatic policy

```cpp
enum class
[[=welder::weld(welder::lang::py)]]
Direction {
    North,
    East,
    South [[=welder::mark::exclude]],   // excluded → not bound
    West                                // keeps its C++ value (3)
};
```

Excluding an enumerator does **not** renumber the rest — `West` is still `3`. A
scoped enum stays qualified:

```pycon
>>> Direction.West.value
3
>>> hasattr(Direction, "South")
False
```

## Unscoped enum — values exported

An unscoped enum also `export_values()`, so its enumerators are visible unqualified
on the enclosing module, mirroring C++:

```cpp
enum [[=welder::weld(welder::lang::py)]]
Signal { Green, Yellow, Red };
```

```pycon
>>> Signal.Red, Red        # both work
(<Signal.Red: 2>, <Signal.Red: 2>)
```

## Scoped enum, opt-in policy

```cpp
enum class
[[=welder::weld(welder::lang::py), =welder::policy::opt_in]]
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
struct [[=welder::weld(welder::lang::py)]]
Compass {
    Direction facing;
};
```

When you [bind a whole namespace](namespaces-modules.md), declaration order handles
this for you — put the enums before the structs that use them.

## Docs

The enum's `doc` becomes the Python docstring. Per-enumerator docs aren't supported
(pybind11's `.value()` takes none); on the **C++** side the
[Doxygen filter](cpp-docs.md) surfaces enumerator docs.

Next: [Inheritance](inheritance.md).
