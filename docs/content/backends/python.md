# Python (pybind11 & nanobind)

welder ships **two** Python backends over the same core:
[pybind11](https://pybind11.readthedocs.io/) (`welder::pybind11`) and
[nanobind](https://nanobind.readthedocs.io/) (`welder::nanobind`). They are close
cousins — same class-handle model, the *same* Python docstring styles
(`welder/backends/python/doc_style.hpp`), the same resolution and bindability rules
— so in welder terms nanobind is nearly a drop-in for pybind11. The difference is
the framework each targets and a short list of feature trade-offs.

Everything in the [guide](../guide/index.md) applies verbatim; this page is the
Python-specific detail.

## Registering a type

The `bind` shape is identical; the module macro and namespace differ:

=== "pybind11"

    ```cpp
    #include <pybind11/pybind11.h>
    #include <pybind11/stl.h>
    #include <welder/backends/python/pybind11/backend.hpp>

    PYBIND11_MODULE(shapes, m) {
        welder::pybind11::bind<Point>(m);
    }
    ```

=== "nanobind"

    ```cpp
    #include <nanobind/nanobind.h>
    #include <nanobind/stl/string.h>       // per-type STL converter headers
    #include <welder/backends/python/nanobind/backend.hpp>

    NB_MODULE(shapes, m) {
        welder::nanobind::bind<Point>(m);
    }
    ```

Or, backend-agnostically, [`WELDER_MODULE(shapes, pybind11)`](../guide/namespaces-modules.md#binding-a-whole-module)
/ `WELDER_MODULE(shapes, nanobind)` to emit `PyInit_shapes` and bind a whole
namespace in one line.

!!! warning "One Python backend per module"

    pybind11 and nanobind **both** emit `PyInit_<name>`, so they cannot coexist in
    the same extension — pick one. (A Python backend *can* share a translation unit
    with a Lua backend, whose symbol is `luaopen_<name>`; see
    [Shipping multiple backends](multiple.md).)

## Feature comparison

Both backends run against welder's *same* shared C++ test cases as a cross-backend
consistency check, so behavior matches wherever the frameworks allow. Where they
differ:

| Feature | pybind11 | nanobind |
|---|---|---|
| Data members (read/write, const → read-only) | ✅ | ✅ |
| Synthesized aggregate field constructor | ✅ | ✅ (placement `__init__`) |
| Named params → keyword arguments | ✅ | ✅ |
| Overloaded methods / constructors | ✅ dispatched | ✅ dispatched |
| Member & free operators → dunders | ✅ | ✅ |
| Enums | `py::enum_`, int-convertible | `IntEnum` (`nb::is_arithmetic`) |
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

## `.pyi` stubs

Your `doc` text and signatures flow into generated
[`.pyi` stubs](../guide/docstrings.md#stubs) so editors and type-checkers see the
bound API — but the two backends source them differently:

- **pybind11** → [pybind11-stubgen](https://github.com/pybind/pybind11-stubgen),
  wired through the CMake helper `welder_pybind11_generate_stubs()` (a `POST_BUILD`
  step; needs an interpreter that has the package installed, e.g. the tests' uv env).
- **nanobind** → its **bundled** stub generator via `nanobind_add_stub` — no extra
  pip dependency (stdlib-only on Python ≥ 3.11).

Both are mypy-checked in welder's own tests.

## Building an extension

=== "pybind11"

    welder's examples use CMake-native `Python_add_library` + `pybind11::headers`
    rather than `pybind11_add_module`, because pybind11 2.13's CMake helper is
    incompatible with the very recent CMake/FindPython here (and the native path
    keeps the module-build flags under our control):

    ```cmake
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
    find_package(pybind11 REQUIRED)

    Python_add_library(shapes MODULE WITH_SOABI example.cpp)
    target_compile_features(shapes PRIVATE cxx_std_26)
    # welder::module -> `import welder;`   welder::pybind11 -> the backend
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
`#include <welder/welder.hpp>` (header-only) — provide the vocabulary before the
backend header. See [the two consumption forms](../guide/getting-started.md#two-consumption-forms).
