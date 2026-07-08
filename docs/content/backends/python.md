# Python (pybind11 & nanobind)

welder ships **two** Python rods over the same core:
[pybind11](https://pybind11.readthedocs.io/) (`welder::rods::pybind11::rod<>`) and
[nanobind](https://nanobind.readthedocs.io/) (`welder::rods::nanobind::rod<>`). They
are close cousins — same class-handle model, the *same* Python docstring styles
(`welder/rods/python/doc_style.hpp`), the same resolution and bindability rules
— so in welder terms nanobind is nearly a drop-in for pybind11. The difference is
the framework each targets and a short list of feature trade-offs.

Everything in the [guide](../guide/index.md) applies verbatim; this page is the
Python-specific detail.

## Registering a type

The `weld_type` shape is identical; the module macro and rod type differ:

=== "pybind11"

    ```cpp
    #include <pybind11/pybind11.h>
    #include <pybind11/stl.h>
    #include <welder/rods/python/pybind11/rod.hpp>

    PYBIND11_MODULE(shapes, m) {
        welder::welder<welder::rods::pybind11::rod<>>::weld_type<Point>(m);
    }
    ```

=== "nanobind"

    ```cpp
    #include <nanobind/nanobind.h>
    #include <nanobind/stl/string.h>       // per-type STL converter headers
    #include <welder/rods/python/nanobind/rod.hpp>

    NB_MODULE(shapes, m) {
        welder::welder<welder::rods::nanobind::rod<>>::weld_type<Point>(m);
    }
    ```

Or, rod-agnostically, [`WELDER_MODULE(shapes, pybind11)`](../guide/namespaces-modules.md#binding-a-whole-module)
/ `WELDER_MODULE(shapes, nanobind)` (from the rod's `module.hpp`) to emit
`PyInit_shapes` and bind a whole namespace in one line.

!!! warning "One Python rod per module"

    pybind11 and nanobind **both** emit `PyInit_<name>`, so they cannot coexist in
    the same extension — pick one. (A Python rod *can* share a translation unit
    with a Lua rod, whose symbol is `luaopen_<name>`; see
    [Shipping multiple rods](multiple.md).)

## Feature comparison

Both rods run against welder's *same* shared C++ test cases as a cross-rod
consistency check, so behavior matches wherever the frameworks allow. Where they
differ:

| Feature | pybind11 | nanobind |
|---|---|---|
| Data members (read/write, const → read-only) | ✅ | ✅ |
| Synthesized aggregate field constructor | ✅ | ✅ (placement `__init__`) |
| Named params → keyword arguments | ✅ | ✅ |
| Overloaded methods / constructors | ✅ dispatched | ✅ dispatched |
| Member & free operators → dunders | ✅ | ✅ |
| Enums | `IntEnum` (`py::native_enum`) | `IntEnum` (`nb::is_arithmetic`) |
| Single welded base | ✅ | ✅ |
| **Multiple / virtual welded bases** | ✅ | ❌ single base only |
| Data-member / function docstrings | ✅ | ✅ |
| Live get/set namespace variables | ✅ | ✅ |
| `.pyi` stub generation | via `pybind11-stubgen` | **bundled** `nanobind_add_stub` |
| Self-contained caster escape hatch | `PYBIND11_TYPE_CASTER` | `NB_TYPE_CASTER` |
| Runtime footprint | larger | smaller / faster |

The one behavioral gap to plan around is **inheritance**: `nb::class_<T, Base>` takes
a single base, so a multi-base or diamond type binds under pybind11 but not nanobind.
welder's shared inheritance test guards the diamond case behind
`WELDER_TEST_MULTIPLE_INHERITANCE` for exactly this reason.

## Operators become dunders

Every welded **member** operator binds to a Python special method ("dunder"), told
apart unary vs. binary by arity. Both Python rods map the *identical* set — this is the
complete list:

| C++ | Python | | C++ | Python |
|---|---|---|---|---|
| `a + b` | `__add__` | | `a == b` | `__eq__` |
| `a - b` | `__sub__` | | `a != b` | `__ne__` |
| `-a` (unary) | `__neg__` | | `a < b` | `__lt__` |
| `+a` (unary) | `__pos__` | | `a > b` | `__gt__` |
| `a * b` | `__mul__` | | `a <= b` | `__le__` |
| `a / b` | `__truediv__` | | `a >= b` | `__ge__` |
| `a % b` | `__mod__` | | `a(...)` | `__call__` |
| `a & b` | `__and__` | | `a[i]` | `__getitem__` |
| `a \| b` | `__or__` | | `a ^ b` | `__xor__` |
| `~a` (unary) | `__invert__` | | `a << b` / `a >> b` | `__lshift__` / `__rshift__` |

Unlike Lua, Python does **not** derive `!=`, `>`, `>=` from their counterparts, so
`operator!=`, `operator>` and `operator>=` are each bound explicitly. In-place
compound assignments (`operator+=`, …) are not mapped — Python falls back to
`a = a + b` via `__add__` — and unary `*` (dereference) and unary `&` (address-of)
are left alone. See the [guide's operator
section](../guide/binding-types.md#overloaded-operators) for the full list of
deliberately-excluded operators.

## `.pyi` stubs

Your `doc` text and signatures flow into generated
[`.pyi` stubs](../guide/docstrings.md#stubs) so editors and type-checkers see the
bound API — but the two rods source them differently:

- **pybind11** → [pybind11-stubgen](https://github.com/pybind/pybind11-stubgen),
  wired through the CMake helper `welder_pybind11_generate_stubs()` (a `POST_BUILD`
  step; needs an interpreter that has the package installed, e.g. the tests' uv env).
- **nanobind** → its **bundled** stub generator via `nanobind_add_stub` — no extra
  pip dependency (stdlib-only on Python ≥ 3.11).

Both are mypy-checked in welder's own tests.

## Building an extension

=== "pybind11"

    welder's examples use CMake-native `Python_add_library` + `pybind11::headers`
    rather than `pybind11_add_module`, because pybind11's CMake helper is
    incompatible with the very recent CMake/FindPython here (and the native path
    keeps the module-build flags under our control):

    ```cmake
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(pybind11 REQUIRED)

    Python_add_library(shapes MODULE WITH_SOABI example.cpp)
    target_compile_features(shapes PRIVATE cxx_std_26)
    # welder::module -> `import welder;`   welder::pybind11 -> the pybind11 rod
    target_link_libraries(shapes PRIVATE welder::module welder::pybind11)
    ```

    (`welder::pybind11` is a small INTERFACE target: welder headers + `pybind11::headers`
    + `Python::Module`. Gated by `WELDER_BUILD_PYBIND11`.)

=== "nanobind"

    nanobind ships its runtime as source compiled *into* each extension, so an
    extension **must** be created with nanobind's own `nanobind_add_module()` — not
    `Python_add_library`. Its config requires Python found first and defines `NB_DIR`:

    ```cmake
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(nanobind CONFIG REQUIRED)   # upstream config, not CMakeDeps

    nanobind_add_module(shapes example.cpp)
    target_compile_features(shapes PRIVATE cxx_std_26)
    target_link_libraries(shapes PRIVATE welder::nanobind)   # headers only
    ```

    (`welder::nanobind` is an INTERFACE target surfacing only welder + nanobind
    headers — the runtime, visibility and link flags come from `nanobind_add_module`.
    Gated by `WELDER_BUILD_NANOBIND`; conan package `nanobind/2.13.0`.)

Because both consume the vocabulary, either can `import welder;` (the module form) or
`#include <welder/vocabulary.hpp>` (header-only) — provide the vocabulary before the
rod header. See [the two consumption forms](../guide/getting-started.md#two-consumption-forms).
