# 10 — Containers by reference

*Source: [`examples/cookbook/10-containers`][src].*

By default a `std::vector<T>` crosses into Python as a **copy** — reading it snapshots
to a `list`, and mutating the snapshot never reaches C++. This recipe binds containers
**opaquely** (by reference) instead: `append` is `push_back` on the live C++ vector,
and a vector of scalars or POD structs is a zero-copy NumPy array. None of the
`WELDER_OPAQUE` / alias boilerplate is hand-written — the
`welder::rods::opaque_containers` rod reflects the welded types at **build time** and
emits it, exactly like the trampoline generator in recipe [05](generated-trampolines.md).

## The four pieces

**The types** (`scene.hpp`) — plain welded structs with plain `std::vector` members.
No `WELDER_OPAQUE`, no aliases, no backend include. Four container flavours are
covered:

```cpp
struct [[=welder::weld(welder::lang::py)]] Vertex { float x, y, z; };   // a POD struct
struct [[=welder::weld(welder::lang::py)]] Entity { std::string name; double health; };

struct [[=welder::weld(welder::lang::py)]] Scene {
    std::vector<Vertex> mesh;      // opaque; POD  -> a structured NumPy array
    std::vector<Entity> actors;    // opaque; a vector of a welded CLASS
    std::vector<double> weights;   // opaque; scalar -> a float64 NumPy array
    [[=welder::rods::python::by_value]] std::vector<int> layers;  // opt out -> list[int]
};
```

`std::vector<Entity>` — a container of a welded class — works because welder's
namespace sweep is **two-phase**: it registers every welded type's *name* before it
binds any container, so `Entity` exists by the time `mesh`/`actors` bind.

**The generator** (`gen.cpp`) — a three-line build-time executable that reflects
namespace `scene` and writes the opaque declarations + aliases:

```cpp
#include <welder/vocabulary.hpp>
#include <welder/rods/python/opaque_containers/module.hpp>
#include "scene.hpp"

WELDER_OPAQUE_CONTAINERS_MAIN(scene)
```

It emits `scene.opaque.hpp` — one `WELDER_OPAQUE(...)` + `using VectorSceneVertex … = …`
per container (a `by_value` member is skipped; names are derived from the type and are
collision-free — a welded element carries its namespace path, `scene::Vertex` →
`VectorSceneVertex`). No NumPy, no pybind11 here: pure reflection to text.

**The wiring** (`CMakeLists.txt`) — `welder_generate_opaque_containers()` builds the
generator, runs it into `scene.opaque.hpp`, and the binding TU compiles the result:

```cmake
welder_generate_opaque_containers(scene_opaque
  SOURCES gen.cpp
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/scene.opaque.hpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scene.hpp)

target_include_directories(scene PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(scene scene_opaque)
```

**The binding TU** (`scene.cpp`) — include the types, then the generated header, then
weld the namespace. That is the whole file:

```cpp
#include "scene.hpp"          // the welded types
#include "scene.opaque.hpp"   // GENERATED — the WELDER_OPAQUE decls + aliases
// … PYBIND11_MODULE(scene, m) { welder::welder<…>::weld_namespace<^^scene>(m); }
```

## What the check asserts

- The generator produced an opaque wrapper per container — `VectorSceneVertex`,
  `VectorSceneEntity`, `VectorDouble` — with no hand-written boilerplate.
- **Reference semantics:** a Python `append` reaches C++ (round-trip helpers
  `total_weight` / `actor_count` read the appended values back).
- The `by_value` member stays a plain `list[int]`.
- **Zero-copy NumPy** (when NumPy is installed): `np.asarray(scene.mesh)` is a
  *structured* array (`x`/`y`/`z` fields) and `np.asarray(scene.weights)` a `float64`
  array — both views over the C++ memory, so a NumPy write reaches the C++ struct. The
  views use the array-interface protocol, so they need no NumPy at build or import time.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/10-containers
