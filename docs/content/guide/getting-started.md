# Getting started

## Toolchain

C++26 reflection is bleeding-edge. welder is written against the **standard**, not
gcc extensions, but today only one compiler implements the papers it needs:

| Requirement | Needed |
|---|---|
| Compiler (P2996 + P3394) | **gcc-16** — the only one so far (`g++-16`, GCC ≥ 16.1); install it from whatever package manager or source build you prefer |
| Build system | CMake ≥ 3.28; the presets drive **Ninja** |
| Packages *(examples/tests only)* | Conan 2 (`conanfile.py`) provisions the backends → pybind11 / nanobind (Python), sol2 (Lua). **Not needed to consume welder** — see [Consuming welder](#consuming-welder). |
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

To build welder's own examples and tests from a clone, Conan provisions the backends;
then configure and build with the preset:

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
matter which rod you register it with:

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

Then a small translation unit per rod registers it. The `#include`s and the
`bind` call differ; **the annotated type above does not.**

=== "Python (pybind11)"

    ```cpp title="shapes.cpp"
    #include <cstdint>
    #include <string>
    #include <welder/vocabulary.hpp>        // annotation vocabulary

    #include <pybind11/pybind11.h>
    #include <pybind11/stl.h>              // std::string conversion (1)
    #include <welder/rods/python/pybind11/rod.hpp>

    // ... Point as above ...

    PYBIND11_MODULE(welder_poc, m) {
        m.doc() = "welder pybind11 proof-of-concept";
        welder::welder<welder::rods::pybind11::rod<>>::weld_type<Point>(m);  // (2)
    }
    ```

    1.  pybind11 needs its STL converters included to move `std::string`,
        `std::vector`, etc. across the boundary. welder's
        [bindability gate](bindability.md) reminds you at compile time if one is
        missing.
    2.  `welder::welder<Rod>` is welder's one entry point, parameterized on a
        **rod** (the backend that lays the bindings down); `weld_type<T>` reflects
        `Point`, resolves which members bind, checks each is representable, and
        emits the pybind11 registration. The bound name defaults to
        `identifier_of(^^T)`; pass a second string to override it. (When you call
        several times, alias it: `using weld = welder::welder<welder::rods::pybind11::rod<>>;`.)

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
    #include <welder/vocabulary.hpp>          // annotation vocabulary

    #include <sol/sol.hpp>
    #include <welder/rods/lua/sol2/rod.hpp>

    // ... Point as above ...

    extern "C" int luaopen_shapes_lua(lua_State* L) {
        sol::state_view lua(L);
        sol::table m = lua.create_table();
        // same core, sol2 emission
        welder::welder<welder::rods::sol2::rod>::weld_type<Point>(m);
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

## Consuming welder

welder is **header-only** and **needs no Conan** — plain CMake wires it in. It exports
the *core* only; you bring your own backend (pybind11/nanobind/sol2/LuaBridge3). Pulled
in as a subproject, welder builds nothing of its own — no backends, no tests, no
install rules — so all it asks of a consumer is a C++26 compiler. Link
`welder::headers`; it carries the vocabulary, the core, C++26 and the `-freflection`
flag.

=== "FetchContent"

    ```cmake
    include(FetchContent)
    FetchContent_Declare(welder
      GIT_REPOSITORY https://github.com/skarndev/welder.git
      GIT_TAG main)
    FetchContent_MakeAvailable(welder)

    target_link_libraries(my_bindings PRIVATE welder::headers)
    ```

=== "Installed + `find_package`"

    Configure with the dev-time build off (nothing of welder's own compiles), install
    the header tree, then depend on it:

    ```bash
    cmake -S welder -B build \
      -DWELDER_BUILD_EXAMPLES=OFF -DWELDER_BUILD_TESTS=OFF \
      -DWELDER_BUILD_PYBIND11=OFF -DWELDER_BUILD_NANOBIND=OFF \
      -DWELDER_BUILD_SOL2=OFF -DWELDER_BUILD_LUABRIDGE=OFF
    cmake --install build --prefix /your/prefix
    ```

    ```cmake
    find_package(welder REQUIRED)   # /your/prefix on CMAKE_PREFIX_PATH
    target_link_libraries(my_bindings PRIVATE welder::headers)
    ```

=== "Conan"

    Optional — only if your project already uses Conan.
    `conan create . -pr:a conan/profiles/gcc16 --build=missing` publishes welder to your
    local cache; a downstream `requires("welder/0.1.0")` then resolves the same
    `find_package(welder)` / `welder::headers`.

`find_package(welder)` (and the FetchContent pull) also define the build helpers —
`welder_pybind11_generate_stubs`, `welder_sol2_add_module`,
`welder_luabridge_add_module`, `welder_luacats_generate_stub` — for producing the
loadable module or stub. Each references its backend's targets only *inside* the call,
so pulling them in is free; you set that backend up yourself.

A consuming TU brings the vocabulary in first, then the rod header:

```cpp
#include <welder/vocabulary.hpp>
#include <welder/rods/python/pybind11/rod.hpp>
```

A C++20 `import welder;` module wrapper is planned but currently deferred — see
[Header-only for now](../header-only.md) for the toolchain reasons why.

Next: the [annotation vocabulary](annotations.md). When you're ready to pick or
combine rods, see the [Rods](../backends/index.md) section.
