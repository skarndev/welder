# Shipping multiple backends

welder's whole premise is that the annotations are written **once** and each backend
is just a different *compile* of the same types. So shipping one library to several
languages — or letting your users choose a Python backend — is a build-system
question, not a code question. This page is the CMake recipe.

## The shape of it

Keep the annotated types in a **backend-free header**, then give each backend a tiny
translation unit that includes that header and the backend, and pick the backend in
`WELDER_MODULE`. Nothing about the types changes between backends.

```text
shapes/
  shapes.hpp        # the annotated namespace — no backend include
  shapes_py.cpp     # + pybind11 (or nanobind) backend  → PyInit_shapes
  shapes_lua.cpp    # + sol2 backend                     → luaopen_shapes
  shapes_stub.cpp   # + luacats backend                  → shapes.lua (editor stub)
```

```cpp title="shapes/shapes.hpp"
#pragma once
#include <welder/welder.hpp>          // vocabulary, header-only (works for every backend)

namespace
[[=welder::doc("A small shapes library built by welder.")]]
shapes {

struct
[[=welder::weld(welder::lang::py, welder::lang::lua),   // welded for BOTH
  =welder::doc("An axis-aligned rectangle.")]]
Rect {
    double w{0.0}, h{0.0};
    Rect() = default;
    Rect(double width, double height) : w{width}, h{height} {}

    [[=welder::doc("The area of the rectangle.")]]
    double area() const { return w * h; }
};

}  // namespace shapes
```

Each backend TU is three lines — include the header, include the backend, stamp the
entry macro:

=== "shapes_py.cpp"

    ```cpp
    #include "shapes/shapes.hpp"
    #include <pybind11/pybind11.h>
    #include <welder/backends/python/pybind11/backend.hpp>

    WELDER_MODULE(shapes, pybind11) {}   // emits PyInit_shapes
    ```

=== "shapes_lua.cpp"

    ```cpp
    #include "shapes/shapes.hpp"
    #include <sol/sol.hpp>
    #include <welder/backends/lua/sol2/backend.hpp>

    WELDER_MODULE(shapes, sol2) {}       // emits luaopen_shapes
    ```

=== "shapes_stub.cpp"

    ```cpp
    #include "shapes/shapes.hpp"
    #include <welder/backends/lua/luacats/backend.hpp>

    WELDER_LUACATS_MAIN(shapes)          // generates shapes.lua at build time
    ```

!!! tip "Use the header-only vocabulary in the shared header"

    `shapes.hpp` includes `<welder/welder.hpp>` (header-only), not `import welder;`.
    That single choice lets *every* backend TU include it — the Lua backend
    [cannot use the module form](lua.md#building-a-loadable-module), and header-only
    works for the Python backends too. This is exactly how welder's own tests reuse
    one C++ case tree across all backends.

## CMake: one library, several modules

The annotated header becomes an INTERFACE target; each backend TU becomes its own
module target that links it. Because Python's `PyInit_shapes` and Lua's
`luaopen_shapes` are **different symbols**, both modules can be named `shapes` and
live side by side — you just put each on its own loader path.

```cmake
# --- the shared, backend-free annotated API ---------------------------------
add_library(shapes_api INTERFACE)
target_include_directories(shapes_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(shapes_api INTERFACE cxx_std_26)
target_link_libraries(shapes_api INTERFACE welder::headers)

# --- Python extension (pybind11) --------------------------------------------
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 REQUIRED)

Python_add_library(shapes_py MODULE WITH_SOABI shapes_py.cpp)
set_target_properties(shapes_py PROPERTIES
    OUTPUT_NAME shapes                                    # import shapes
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/python)
target_link_libraries(shapes_py PRIVATE shapes_api welder::pybind11)

# --- Lua module (sol2) ------------------------------------------------------
find_package(sol2 REQUIRED)

welder_sol2_add_module(shapes_lua shapes_lua.cpp)         # bare .so, host-symbol model
set_target_properties(shapes_lua PROPERTIES
    OUTPUT_NAME shapes                                    # require "shapes"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lua)
target_link_libraries(shapes_lua PRIVATE shapes_api)

# --- LuaCATS editor stub (build-time, no Lua/sol2 needed) -------------------
welder_luacats_generate_stub(shapes_stub
    SOURCES shapes_stub.cpp
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/lua/shapes.lua)
```

Load them from their respective paths:

```bash
PYTHONPATH=build/.../python python3 -c "import shapes; print(shapes.Rect(2,3).area())"
LUA_CPATH='build/.../lua/?.so' lua -e 'print(require("shapes").Rect(2,3):area())'
```

The target *names* (`shapes_py`, `shapes_lua`) only have to be unique in CMake; it's
`OUTPUT_NAME` (the file) and the `WELDER_MODULE` token (the entry symbol) that must
equal the module name the loader asks for.

## Choosing between the two Python backends

pybind11 and nanobind **both** emit `PyInit_<name>`, so they can't share a module —
but you can select which one to build at configure time. Point one TU at either
backend header and branch in CMake:

```cmake
set(WELDER_PYTHON_BACKEND pybind11 CACHE STRING "pybind11 or nanobind")

if(WELDER_PYTHON_BACKEND STREQUAL nanobind)
    find_package(nanobind CONFIG REQUIRED)
    nanobind_add_module(shapes_py shapes_py.cpp)          # required for nanobind
    target_link_libraries(shapes_py PRIVATE shapes_api welder::nanobind)
else()
    find_package(pybind11 REQUIRED)
    Python_add_library(shapes_py MODULE WITH_SOABI shapes_py.cpp)
    target_link_libraries(shapes_py PRIVATE shapes_api welder::pybind11)
endif()
set_target_properties(shapes_py PROPERTIES OUTPUT_NAME shapes)
```

Have `shapes_py.cpp` select the matching backend the same way (e.g. behind a
`-DWELDER_PYTHON_BACKEND=…` compile definition, or two thin TUs), so
`WELDER_MODULE(shapes, pybind11)` / `(shapes, nanobind)` matches the CMake branch.
See the [Python backends comparison](python.md#feature-comparison) to pick.

!!! note "Building both Python backends as variants"

    Because they collide on `PyInit_shapes`, you can't `import` both from one file.
    If you want both (say, to benchmark), build them as two separate modules with
    distinct names (`shapes_pb`, `shapes_nb`) — two targets, one shared `shapes_api`.

## Advanced: one shared object, two languages

Since the entry symbols differ, a *single* `.so` can technically expose both
`PyInit_shapes` and `luaopen_shapes` (two `WELDER_MODULE`s in one TU). In practice
the recommended layout is still **one module per language**: `nanobind_add_module`
and `welder_sol2_add_module` set conflicting link models (nanobind compiles its
runtime in; the Lua module must resolve `lua_*` from its host and bundle nothing), so
merging them into one target is fragile. Keep them as separate targets sharing
`shapes_api` — you get the same "write once" benefit without fighting the link.
