# Stubs

A compiled extension is a black box to every tool that reads *source*: an editor
can't complete `Rect(`, a type checker can't tell `area()` returns a `float`,
and your carefully written docstrings never show up in a tooltip. A **stub** is
the answer — a source-form description of the binary module's surface that the
tooling reads instead. Each target language has its own dialect, and welder
generates both:

| Language | Stub dialect | Read by |
|---|---|---|
| Python | [`.pyi` stub file](https://typing.python.org/en/latest/guides/writing_stubs.html) | IDEs, [mypy](https://mypy-lang.org/), pyright, … |
| Lua | [LuaCATS](https://luals.github.io/wiki/annotations/) `---@meta` file | the [Lua language server](https://luals.github.io/) |

For Python the stub is a *nicety* (the runtime module still carries `__doc__`
and signatures); for Lua it is the **only** home your docs have — Lua has no
runtime docstring slot at all, so without the stub the `doc` text you wrote
reaches no Lua user.

## One source, both stubs

The same annotated type feeds both generators — nothing stub-specific is ever
written by hand:

=== ":simple-cplusplus: C++ (the one source)"

    ```cpp
    namespace shapes {

    struct
    [[=welder::weld(welder::lang::py, welder::lang::lua),
      =welder::doc("An axis-aligned rectangle.")]]
    Rect {
        [[=welder::doc("The width.")]]  double w{0.0};
        [[=welder::doc("The height.")]] double h{0.0};

        Rect() = default;
        Rect(double width, double height);

        [[=welder::doc("The area of the rectangle."),
          =welder::returns("width times height")]]
        double area() const;
    };

    }  // namespace shapes
    ```

=== ":simple-python: Python (`shapes.pyi`)"

    ```python
    class Rect:
        """
        An axis-aligned rectangle.
        """
        @typing.overload
        def __init__(self) -> None: ...
        @typing.overload
        def __init__(self, width: float, height: float) -> None: ...
        @property
        def w(self) -> float:
            """
            The width.
            """
        @w.setter
        def w(self, arg0: float) -> None: ...
        @property
        def h(self) -> float:
            """
            The height.
            """
        @h.setter
        def h(self, arg0: float) -> None: ...
        def area(self) -> float:
            """
            The area of the rectangle.

            Returns:
                width times height
            """
    ```

=== ":simple-lua: Lua (`shapes.lua`)"

    ```lua
    ---@meta

    shapes = {}

    --- An axis-aligned rectangle.
    ---@class shapes.Rect
    ---@field w number The width.
    ---@field h number The height.
    shapes.Rect = {}

    ---@return shapes.Rect
    ---@overload fun(width: number, height: number): shapes.Rect
    function shapes.Rect.new() end

    --- The area of the rectangle.
    ---@return number width times height
    function shapes.Rect:area() end
    ```

Every piece of the [doc vocabulary](docstrings.md) — summaries, parameter docs,
`returns` — lands in the stub, and a [name style or `weld_as`](naming.md)
rename is honoured in it exactly as in the runtime binding.

## Python: `.pyi`

The two Python rods produce the same kind of stub through different tools —
both **scrape the built extension** (import it, walk its surface), so the stub
is generated as a post-build step of the module target:

| Rod | Tool | Wiring |
|---|---|---|
| **pybind11** | [pybind11-stubgen](https://github.com/pybind/pybind11-stubgen) (a pip package) | welder's CMake helper `welder_pybind11_generate_stubs()` |
| **nanobind** | nanobind's [bundled stub generator](https://nanobind.readthedocs.io/en/latest/typing.html) (no pip dependency; stdlib-only on Python ≥ 3.11) | upstream `nanobind_add_stub()` |

Because the tool imports the module, the interpreter that runs it must be able
to load the extension (ABI match) and — for pybind11 — have `pybind11-stubgen`
installed. The rod-specific notes live on the
[Python rods page](../backends/python.md#pyi-stubs).

## Lua: LuaCATS (`---@meta`)

The Lua stub cannot be scraped: a loaded sol2/LuaBridge3 module exposes nothing
introspectable. So welder generates it the other way around — the build-time
`welder::rods::luacats::rod` walks the *same* welded types through the same
core driver as the runtime rods and **emits the LuaCATS text directly from
reflection**, before any module is ever loaded. It needs no Lua and no binding
framework at all, just the reflecting compiler; one generator TU plus the
`welder_luacats_generate_stub()` CMake helper produce the `.lua` file.

The generator TU, the CMake wiring, the C++→LuaCATS type map and the few
metamethods LuaCATS cannot express are covered on the
[Lua rod page](../backends/lua.md#stubs-luacats).

!!! example "In the cookbook"

    [Recipe 07 — One library, two languages](../cookbook/multilang.md) builds
    one header into a Python module with a `.pyi` stub *and* a Lua module with
    a LuaCATS stub, CI-asserted.

Next: [Naming conventions](naming.md).