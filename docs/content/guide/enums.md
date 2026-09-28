# Enums

A welded enum — scoped or unscoped — binds via the same `weld_type<T>` entry point
(dispatched to the enum path by `is_enum_v`). Each **enumerator resolves like a
data member**: the enum's `policy` plus per-enumerator `exclude`/`include` marks
decide what binds. What it becomes in the target language differs:

- **Python** — a stdlib `enum.IntEnum` (pybind11 `py::native_enum`, nanobind
  `nb::is_arithmetic`; int-convertible) — or an `enum.IntFlag` for a
  [`flags` enum](#bitmask-flags-enums).
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

## Bitmask (flags) enums

A stdlib `IntEnum` rejects any value that is not a named enumerator — converting
`Direction(0x1234)` raises `ValueError`. That is correct for a closed value set,
and wrong for a **bitmask**, whose instances are OR-combinations of the named
bits (binary file formats also routinely ship bits no enumerator names).
Declare a bitmask with the [`flags` annotation](annotations.md#flags-bitmask-enums):

```cpp
enum class [[=welder::weld, =welder::flags]] Styles : std::uint32_t {
    Bold = 0x1, Italic = 0x2, Underline = 0x4,
};
```

Python then binds an **`enum.IntFlag`** (nanobind `nb::is_flag()`, pybind11
`native_enum … "enum.IntFlag"`):

```pycon
>>> Styles.Bold | Styles.Italic          # combining stays inside the type
<Styles.Bold|Italic: 3>
>>> Styles(0x81)                         # unnamed bit 0x80: kept, not rejected
<Styles.Bold|128: 129>
>>> Styles.Bold in (Styles.Bold | Styles.Underline)
True
```

`IntFlag` still inherits `int`, so existing `value & 0x4` call sites keep
working. The Lua rods ignore the annotation — their enums cross as plain
integers, which combine freely anyway. An enum-typed **field** of a POD struct
also keeps its [zero-copy structured-array
view](containers.md#pod-structs-structured-arrays): NumPy has no enum dtype, so
the field views as the enum's fixed underlying integer.

!!! note "Stub generators and `IntFlag`"

    Both Python stub generators leave `IntFlag` artifacts a strict `mypy`
    rejects (nanobind emits the class-dict alias `__str__ = __repr__` *before*
    the `__repr__` it references; `pybind11-stubgen` renders flag members as
    annotations, which mypy counts as "an enum with zero members"). welder
    ships `tools/welder_stub_sanitizer.py` — point it at a generated stubs
    directory between stub generation and type-checking, as welder's own test
    gates do.

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

!!! note "Per-enumerator docs ride in the enum's class docstring"

    A `doc` on an individual **enumerator** has no per-member docstring slot the
    Python stub tools surface (a stub lists a member as a bare `Name = value`), so
    welder folds it into the enum's **class** docstring as an *Attributes* section —
    the one place `pybind11-stubgen` / nanobind's stubgen carries it into the
    `.pyi`:

    ```python
    class Direction(enum.IntEnum):
        """
        A compass bearing.

        Attributes:
            North: towards the pole
            East: sunrise
        """
    ```

    The section is spelled in the rod's [docstring
    style](docstrings.md#choosing-a-docstring-style) — Google `Attributes:`, NumPy
    underlined `Attributes`, Sphinx `:var:`. An
    undocumented enumerator contributes no line; an excluded one appears nowhere.
    The **Lua** rods have no runtime docstring, so they ignore it at load time; the
    [LuaCATS stub](../backends/lua.md#stubs-luacats) instead emits each enumerator's
    doc as a `---` comment above its entry in the `---@enum` table (the language
    server attaches that to the member, shown on hover/completion). The doc also
    reaches the C++ reference, where the [Doxygen filter](cpp-docs.md) surfaces it
    as a trailing `/**< */` comment. How docs flow in general is covered later, in
    [Docstrings](docstrings.md).

Next: [Inheritance](inheritance.md).
