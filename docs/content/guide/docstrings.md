# Docstrings

Documentation is part of the vocabulary. You write it **once**, on the C++
declaration, and it reaches every audience welder can carry it to: the target
language's runtime docstring (Python `__doc__`, via the binding rod), the C++
API reference (via the [Doxygen filter](cpp-docs.md)), and generated stubs
([`.pyi`](#stubs) for Python, [LuaCATS `---@meta`](../backends/lua.md#stubs-luacats)
for Lua — the latter is where Lua's docs live, since Lua has no runtime slot).

| Annotation | Applies to | Becomes |
|---|---|---|
| `doc("text")` | class, namespace, function, parameter, data member | the summary docstring |
| `returns("text")` | function | a `Returns:` block |
| `tparam("T", "text")` | template (repeatable, ordered) | template-parameter docs |

!!! example "In the cookbook"

    [Recipe 07 — One library, two languages](../cookbook/multilang.md) carries one
    set of `doc`/`returns` annotations into Python `__doc__`, a `.pyi` stub *and*
    a LuaCATS stub.

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
    rides on the function as a distinct spec type (`welder::detail::return_doc_spec`), told apart
    from the summary by spec type — which also keeps the door open for future
    `Raises:` / `Note:` blocks without re-breaking the style API.

## Choosing a docstring style

The style is a property of the **rod**, selected through its `DocStyle` template
parameter. The Python rods ship three, matching the dialects the Python ecosystem's
doc tools understand — pick one by naming it in the rod type:

| Style | Rod | Renders as |
|---|---|---|
| `welder::rods::python::google_style` | `rod<>` (default) | `Args:` / `Returns:` blocks (Sphinx **Napoleon**) |
| `welder::rods::python::numpy_style` | `rod<numpy_style>` | underlined `Parameters` / `Returns` sections (**numpydoc**; also Napoleon) |
| `welder::rods::python::sphinx_style` | `rod<sphinx_style>` | `:param name:` / `:returns:` reST field lists (**autodoc** native) |

```cpp
#include <welder/rods/python/pybind11/rod.hpp>
namespace py = welder::rods::python;

PYBIND11_MODULE(mymod, m) {
    // default (Google):        welder::rods::pybind11::rod<>
    // NumPy:  welder::rods::pybind11::rod<py::numpy_style>
    // Sphinx: welder::rods::pybind11::rod<py::sphinx_style>
    welder::welder<welder::rods::pybind11::rod<py::numpy_style>>
        ::weld_namespace<^^mymod>(m);
}
```

The same `scale` above, welded through `rod<py::numpy_style>` and
`rod<py::sphinx_style>` respectively:

```pycon
>>> print(scale.__doc__)          # numpy_style
Scale a length by a factor.

Parameters
----------
length
    the length to scale
factor
    the multiplier

Returns
-------
the scaled length
```

```pycon
>>> print(scale.__doc__)          # sphinx_style
Scale a length by a factor.

:param length: the length to scale
:param factor: the multiplier
:returns: the scaled length
```

welder has no target-language type text to place after numpydoc's `name : type`
colon, so it emits the bare `name` form (which numpydoc accepts). The choice is
per-`welder::welder` instantiation, so different modules — or even different
submodules — can carry different styles.

!!! note "Styles are `constexpr`"

    Each style's `format()` is a plain `constexpr` `std::string` assembly, so it is
    unit-testable by `static_assert` (like [`cleandoc`](#multiline-docstrings)) and
    usable in any compile-time context. That is deliberately *not* written with
    `std::format`: `std::format` is not `constexpr` in the standard library (as of
    gcc-16), so a `constexpr` docstring builder cannot call it — the compile-time
    doc paths (`cleandoc`, annotation reading) rule it out, and the styles stay
    hand-rolled for one consistent story.

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

## Data members

A `doc` on a **data member** rides onto its Python attribute. pybind11 binds
members as *properties* (data descriptors on the class), so the doc becomes the
property's `__doc__` — and flows into the `.pyi` [stubs](#stubs):

```cpp
struct
[[=welder::weld(welder::lang::py), =welder::doc("A circle.")]]
Circle {
    [[=welder::doc("The radius.")]] double r{0.0};
    [[=welder::doc("The immutable id.")]] const int id{0};  // read-only
};
```

```pycon
>>> Circle.r.__doc__
'The radius.'
```

A `const` member is bound read-only (get, no set); a mutable one is read/write.
Only the getter's doc is surfaced — a Python `property` has a single `__doc__` —
so there is no separate setter docstring.

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
`tparam_docs<Ent>()` off an instantiation for rod docstrings.

## What rods ignore

- **Namespace variable docs** are ignored by binding rods — a bound module
  attribute has no `__doc__`. (Class *data members* do carry docs, via properties —
  see [Data members](#data-members) above; and the Doxygen filter surfaces variable
  docs on the C++ side.)
- **Per-enumerator docs** aren't supported by pybind11's `.value()`.

!!! note "Docs are stored inline"

    Doc text is stored inline as a `welder::detail::fixed_string`. A `const char*` to a string
    literal isn't a permitted annotation constant on gcc-16, so welder can't hold
    a pointer — it holds the characters.

## Stubs

welder docstrings flow into generated `.pyi` stubs via
[pybind11-stubgen](https://github.com/pybind/pybind11-stubgen), wired through the
CMake helper `welder_pybind11_generate_stubs()`. So your `doc` text also lands in
the types your Python users' editors and type-checkers see.

Lua has no runtime docstring slot, so there the docs live only in a stub: welder
emits a **LuaCATS (`---@meta`) definition file** — reflection-generated at build
time — carrying the same `doc`/`returns`/parameter text. See [Stubs
(LuaCATS)](../backends/lua.md#stubs-luacats) in the Lua rod guide.

Next: [The bindability gate](bindability.md).
