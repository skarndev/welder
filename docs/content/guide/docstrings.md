# Docstrings

Documentation is part of the vocabulary. You write it **once**, on the C++
declaration, and it reaches *two* audiences: the target language's `__doc__` (via
the binding backend) and the C++ API reference (via the
[Doxygen filter](cpp-docs.md)).

| Annotation | Applies to | Becomes |
|---|---|---|
| `doc("text")` | class, namespace, function, parameter | the summary docstring |
| `returns("text")` | function | a `Returns:` block |
| `tparam("T", "text")` | template (repeatable, ordered) | template-parameter docs |

## `doc` and `returns`

```cpp
struct
[[=welder::weld(welder::lang::py), =welder::doc("A 2-D vector.")]]
Vec2 {
    double x{0.0}, y{0.0};

    [[=welder::doc("Euclidean length."), =welder::returns("the magnitude")]]
    double length() const;
};

[[
  =welder::weld(welder::lang::py),
  =welder::doc("Scale a length by a factor."),
  =welder::returns("the scaled length")
]]
double scale(
    [[=welder::doc("the length to scale")]] double length,
    [[=welder::doc("the multiplier")]] double factor);
```

The docstring engine (`doc.hpp`, backend-agnostic) folds the pieces —
summary + parameter docs + return doc — under a pluggable **style**. The default
`google_style` produces `Args:` / `Returns:` blocks:

```pycon
>>> print(scale.__doc__)
scale(length: float, factor: float) -> float

Scale a length by a factor.

Args:
    length: the length to scale
    factor: the multiplier

Returns:
    the scaled length
```

!!! info "Why `returns` is separate from `doc`"

    A return value isn't a reflectable entity, so its doc can't live *on* one. It
    rides on the function as a distinct spec type (`return_doc_spec`), told apart
    from the summary by spec type — which also keeps the door open for future
    `Raises:` / `Note:` blocks without re-breaking the style API.

## Multiline docstrings

Function docs often carry examples that span several lines, so use a **raw string
literal** — it's still a `const char[N]`, so newlines, blank lines, quotes and
backslashes all flow through `doc` to `__doc__`.

You can indent the text to line up with the surrounding source: welder **dedents**
it the way Python's [`inspect.cleandoc`](https://docs.python.org/3/library/inspect.html#inspect.cleandoc)
(PEP 257) does. The common leading indentation is stripped, leading/trailing blank
lines are trimmed, and an example block's *relative* extra indentation is kept.

```cpp
[[
  =welder::weld(welder::lang::py),
  =welder::doc(R"(
      Parse an integer.

      Example:
          >>> parse("42")
          42
  )")
]]
int parse(const std::string& text);
```

```pycon
>>> print(parse.__doc__)
Parse an integer.

Example:
    >>> parse("42")
    42
```

The source indentation is gone; the four-space example indent — relative to the
prose — survives.

!!! tip "Dedent details"

    Only the whitespace **common** to every line (after the first) is removed, so
    relative structure is preserved. The first line is stripped separately, so both
    `doc(R"(First line here …` and a doc that opens on its own line work. Indent
    with **spaces** — tabs are treated as single characters, not expanded.

Continuation lines of a **parameter** or **return** doc are likewise kept indented
under their `Args:` / `Returns:` block, so a multiline entry reads as one.

## `tparam` — documenting templates

Template parameters aren't reflectable entities either, so their docs ride on the
**template itself**, keyed by name and ordered:

```cpp
template <class T>
struct
[[
  =welder::weld(welder::lang::py),
  =welder::tparam("T", "the stored element type")
]]
Box { T value; };
```

`tparam` becomes an `@tparam` in the C++ docs, and is read back via
`tparam_docs<Ent>()` off an instantiation for backend docstrings.

## What backends ignore

- **Variable docs** are ignored by binding backends — Python has no attribute
  `__doc__`. (The Doxygen filter *does* surface them on the C++ side.)
- **Per-enumerator docs** aren't supported by pybind11's `.value()`.

!!! note "Docs are stored inline"

    Doc text is stored inline as a `fixed_string`. A `const char*` to a string
    literal isn't a permitted annotation constant on gcc-16, so welder can't hold
    a pointer — it holds the characters.

## Stubs

welder docstrings flow into generated `.pyi` stubs via
[pybind11-stubgen](https://github.com/pybind/pybind11-stubgen), wired through the
CMake helper `welder_pybind11_generate_stubs()`. So your `doc` text also lands in
the types your Python users' editors and type-checkers see.

Next: [The bindability gate](bindability.md).
