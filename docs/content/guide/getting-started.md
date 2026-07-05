# Getting started

## Toolchain

C++26 reflection is bleeding-edge. welder is written against the **standard**, not
gcc extensions, but today only one compiler implements the papers it needs:

| Requirement | This machine |
|---|---|
| Compiler (P2996 + P3394) | **gcc-16** — the only one so far (`g++-16`, Homebrew GCC 16.1.0) |
| Build system | CMake ≥ 3.28 (for `FILE_SET CXX_MODULES`) + **Ninja** (modules need it) |
| Packages | Conan 2 (`conanfile.py`) → pybind11 / nanobind (Python); sol2 + Lua (Lua) |
| Python | a `python3` with development headers (for the Python modules) |
| Lua | headers via conan `sol2` (pulls Lua); a Lua interpreter to load the module |

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

The example modules are then loadable — a Python extension and a Lua C module,
both built from the *same* welder core:

=== "Python"

    ```bash
    PYTHONPATH=build/welder-gcc16/examples/python_poc \
      python3 -c "import welder_poc as w; p=w.Point(); p.x=1.5; print(p.x)"
    ```

=== "Lua"

    ```bash
    LUA_CPATH='build/welder-gcc16/examples/lua_poc/?.so' \
      lua -e 'local s=require("shapes_lua"); local r=s.Rect(3,4); print(r:area())'
    ```

## Your first type

welder's promise is that **one annotated type binds to every language you weld it
for**. Here is a struct welded for *both* Python and Lua — the default policy
(`automatic`) reflects every member unless excluded. This C++ is identical no
matter which backend you register it with:

```cpp
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]  // (1)
Point {
    double x{0.0};
    double y{0.0};

    [[=welder::mark::exclude]]  // (2)
    std::uint64_t internal_id{0};
};
```

1.  `weld` is *required* to bind, and lists the target languages. Without it,
    `bind<Point>` is a no-op-by-contract. Weld for only the languages you want.
2.  `mark::exclude` (no argument) hides `internal_id` from **all** welded
    languages. `mark::exclude(welder::lang::py)` would hide it from Python only.

Then a small translation unit per backend registers it. The `#include`s and the
`bind` call differ; **the annotated type above does not.**

=== "Python (pybind11)"

    ```cpp title="shapes.cpp"
    #include <cstdint>
    #include <string>
    import welder;                          // annotation vocabulary (module form)

    #include <pybind11/pybind11.h>
    #include <pybind11/stl.h>              // std::string conversion (1)
    #include <welder/backends/python/pybind11/backend.hpp>

    // ... Point as above ...

    PYBIND11_MODULE(welder_poc, m) {
        m.doc() = "welder pybind11 proof-of-concept";
        welder::pybind11::bind<Point>(m);  // (2)
    }
    ```

    1.  pybind11 needs its STL converters included to move `std::string`,
        `std::vector`, etc. across the boundary. welder's
        [bindability gate](bindability.md) reminds you at compile time if one is
        missing.
    2.  `bind<T>` reflects `Point`, resolves which members bind, checks each is
        representable, and emits the pybind11 registration. The bound name defaults
        to `identifier_of(^^T)`; pass a second string to override it.

    ```pycon
    >>> import welder_poc as w
    >>> p = w.Point(); p.x = 1.5
    >>> p.x
    1.5
    >>> hasattr(p, "internal_id")
    False
    ```

=== "Lua (sol2)"

    ```cpp title="shapes_lua.cpp"
    #include <cstdint>
    #include <string>
    #include <welder/welder.hpp>          // vocabulary (header-only — see note)

    #include <sol/sol.hpp>
    #include <welder/backends/lua/sol2/backend.hpp>

    // ... Point as above ...

    extern "C" int luaopen_shapes_lua(lua_State* L) {
        sol::state_view lua(L);
        sol::table m = lua.create_table();
        welder::sol2::bind<Point>(m);      // same core, sol2 emission
        return sol::stack::push(L, m);
    }
    ```

    ```lua
    local w = require("shapes_lua")
    local p = w.Point()      -- or w.Point.new()
    p.x = 1.5
    print(p.x)               --> 1.5
    print(p.internal_id)     --> nil (excluded)
    ```

welder synthesizes a field constructor for a baseless **aggregate** when every
field binds, so `Point(1.0, 2.0)` also works — see
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

Backends are *always* header-only.

!!! note "The Lua backend is header-only *only*"

    sol2's `<luaconf.h>` doesn't survive C++20 module dependency scanning, so a Lua
    TU must consume welder with `#include <welder/welder.hpp>`, never `import
    welder;`. The Python backends work with either form. Details on the
    [Lua backend page](../backends/lua.md).

Next: the [annotation vocabulary](annotations.md). When you're ready to pick or
combine backends, see the [Backends](../backends/index.md) section.
