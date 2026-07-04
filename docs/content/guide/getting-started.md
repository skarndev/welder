# Getting started

## Toolchain

C++26 reflection is bleeding-edge. welder is written against the **standard**, not
gcc extensions, but today only one compiler implements the papers it needs:

| Requirement | This machine |
|---|---|
| Compiler (P2996 + P3394) | **gcc-16** — the only one so far (`g++-16`, Homebrew GCC 16.1.0) |
| Build system | CMake ≥ 3.28 (for `FILE_SET CXX_MODULES`) + **Ninja** (modules need it) |
| Packages | Conan 2 (`conanfile.py`) → pybind11 |
| Python | a `python3` with development headers (for the pybind11 module) |

Reflection/module flags are isolated in the `welder_flags` CMake target and gated
on the compiler id, so nothing gcc-specific leaks into the public targets. As
Clang/MSVC catch up, add a branch there.

!!! tip "Reflection flags"

    Just `-std=c++26 -freflection` on gcc-16 — annotations are included, no
    separate flag. Reflect with `^^Thing`, splice with `[: r :]`,
    pointer-to-member with `&[:member:]`.

## Build

```bash
conan install . -pr:a conan/profiles/gcc16 --build=missing
cmake --preset welder-gcc16
cmake --build --preset welder-gcc16
```

Both example Python modules are then importable:

```bash
PYTHONPATH=build/welder-gcc16/examples/python_poc \
  python3 -c "import welder_poc as w; p=w.Point(); p.x=1.5; print(p.x)"
```

## Your first module

A struct welded for Python. The default policy (`automatic`) reflects every member
unless excluded.

```cpp title="shapes.cpp"
#include <cstdint>
#include <string>

import welder;                          // annotation vocabulary (module form)

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>              // std::string conversion (1)
#include <welder/backends/python/pybind11/backend.hpp>

struct
[[=welder::weld(welder::lang::py)]]    // (2)
Point {
    double x{0.0};
    double y{0.0};

    [[=welder::mark::exclude(welder::lang::py)]]  // (3)
    std::uint64_t internal_id{0};
};

PYBIND11_MODULE(welder_poc, m) {
    m.doc() = "welder pybind11 proof-of-concept";
    welder::pybind11::bind<Point>(m);  // (4)
}
```

1.  pybind11 needs its STL converters included to move `std::string`,
    `std::vector`, etc. across the boundary. welder's
    [bindability gate](bindability.md) will remind you at compile time if one is
    missing.
2.  `weld` is *required* to bind, and lists the target languages. Without it,
    `bind<Point>` is a no-op-by-contract.
3.  Per-language exclusion: `internal_id` is hidden from Python. `mark::exclude`
    with no argument would hide it from **all** welded languages.
4.  `bind<T>` reflects `Point`, resolves which members bind, checks each is
    representable, and emits the pybind11 registration. The Python name defaults
    to `identifier_of(^^T)`; pass a second string to override it.

The result:

```pycon
>>> import welder_poc as w
>>> p = w.Point()
>>> p.x = 1.5
>>> p.x
1.5
>>> hasattr(p, "internal_id")
False
```

welder synthesizes a field constructor for a baseless **aggregate** when every
field binds, so `w.Point(1.0, 2.0)` also works — see
[Binding a type](binding-types.md#constructors).

## Two consumption forms

welder is fundamentally **header-only**, with one optional module wrapper so you
can `import welder;`. Pick whichever you prefer — they are equivalent — but always
provide the vocabulary before the backend header:

=== "Module"

    ```cpp
    import welder;
    #include <welder/backends/python/pybind11/backend.hpp>
    ```

=== "Header-only"

    ```cpp
    #include <welder/welder.hpp>
    #include <welder/backends/python/pybind11/backend.hpp>
    ```

Backends are *always* header-only. Next: the [annotation vocabulary](annotations.md).
