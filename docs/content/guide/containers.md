# Containers by reference

By default a `std::vector<T>` or `std::map<K, V>` crosses into Python by **copy**.
pybind11's `<pybind11/stl.h>` (nanobind's `<nanobind/stl/…>` headers) convert it to a
`list`/`dict` on every access — so reading a container member *snapshots* it, and
mutating the snapshot never reaches C++:

```python
obj.values          # a fresh list[int], copied from the C++ vector
obj.values.append(1)  # mutates the throwaway copy — the C++ vector is unchanged
```

For a dynamic container that is rarely what you want. welder can instead bind a
container **opaquely** — *by reference* — so mutation writes through, `push_back`
shows up as `append`, and a vector of scalars or POD structs exposes its raw buffer to
NumPy **zero-copy** (scalars as a typed array, POD structs as a structured one).

!!! note "Python only"

    This is a Python-rod feature. The Lua runtimes (sol2, LuaBridge3) already give
    containers reference semantics structurally, so they need no opt-in — welding a
    container alias for a Lua rod is a compile error.

## The opt-in: weld an alias, declare it opaque

The trigger reuses the [template-instantiation alias](templates.md) mechanism — you
weld a **namespace-scope alias** to the container — plus one framework declaration
that makes the container opaque:

```cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>          // other vectors/maps still convert by value
#include <welder/rods/python/pybind11/rod.hpp>

WELDER_OPAQUE(std::vector<int>)    // ← required, at namespace scope, before the module

namespace app {
using IntVector [[=welder::weld(welder::lang::py)]] = std::vector<int>;  // ← binds as IntVector

struct [[=welder::weld(welder::lang::py)]] Histogram {
    std::vector<int> bins{};       // now a live IntVector, not a list snapshot
};
}

PYBIND11_MODULE(app, m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^app>(m);
}
```

`WELDER_OPAQUE(T)` is welder's neutral spelling of the framework's opaque declaration
(`PYBIND11_MAKE_OPAQUE` / nanobind's `NB_MAKE_OPAQUE`). It **must** appear at namespace
scope, before the module. Without it the container still binds, but the copy caster
wins and you get value semantics back (nanobind is stricter — it refuses to compile a
`bind_vector` for a container whose stl caster is visible, and the error names
`NB_MAKE_OPAQUE` as the fix).

The name comes from the alias (here `IntVector`), and — like any welded alias — a
`[[=welder::weld_as(...)]]` on it overrides that name.

## What you get

The bound container is a real reference-semantic class. Mutation persists:

```python
import app
h = app.Histogram()
h.bins.append(10)      # push_back, straight onto the C++ vector
h.bins.append(20)
h.bins[0] = 99         # __setitem__ writes through
len(h.bins)            # 2 — h.bins IS the C++ vector, not a copy
```

`std::vector` gains the full mutable-sequence protocol — `append`, `extend`, `insert`,
`pop`, `__getitem__`/`__setitem__`, slicing, `__len__`, `__iter__`, `__contains__`;
`std::map` / `std::unordered_map` become a mutable mapping (`__getitem__`/`__setitem__`,
`__contains__`, `keys`/`values`/`items`, iteration over keys).

### Zero-copy NumPy / ctypes

When a `std::vector`'s element type is a **scalar** (an arithmetic type, not `bool`),
the bound class also exposes its contiguous `data()` buffer with no copy:

```python
import numpy as np
a = np.asarray(h.bins)   # a VIEW over the C++ buffer — a.flags['OWNDATA'] is False
a[0] = 7                 # writes straight into the C++ vector
```

`memoryview(h.bins)` and `ctypes` (`(ctypes.c_int * len(h.bins)).from_buffer(h.bins)`)
work the same way. This is the safe, idiomatic route to the raw pointer — a NumPy view
tracks reallocation on the next `asarray`, where a stored raw address would dangle.

#### POD structs → structured arrays

A `std::vector` of a **plain-old-data struct** (trivially-copyable, standard-layout,
all-arithmetic fields — `struct Vec3 { float x, y, z; }`) is exposed as a NumPy
**structured** array, again zero-copy and writable:

```python
a = np.asarray(mesh.vertices)   # dtype [('x','<f4'),('y','<f4'),('z','<f4')], a view
a[0]['x'] = 1.5                 # writes straight into the C++ struct
```

welder reflects the struct's fields (names, types, offsets — padding included) and
serves NumPy the layout through the `__array_interface__` protocol — a plain attribute,
so this needs **no NumPy at build or import time**, and works identically on both Python
rods. It fires automatically for any POD-struct element; a type with a vtable (a
virtual/overridable type), a `std::string`, or a pointer isn't trivially copyable, so it
simply gets no array view (there's no meaningful one).

## Generating the boilerplate

Writing `WELDER_OPAQUE(T)` + a welded alias for every container type is repetitive —
and, because the two straddle different scopes, a single macro can't collapse them. So
welder ships a **generator** that writes them for you, the same build-time,
reflection-driven model as the [trampoline generator](inheritance.md#generating-trampolines-automatically):
it reflects your welded types, finds every scalar-element container they use, and emits
a `.hpp` of the `WELDER_OPAQUE` declarations + aliases. You include that header and
never hand-write the boilerplate.

Point the CMake helper at a one-line generator TU:

```cpp
// app_opaque_gen.cpp
#include <welder/vocabulary.hpp>
#include <welder/rods/python/opaque_containers/module.hpp>
#include "app_types.hpp"                 // your welded types
WELDER_OPAQUE_CONTAINERS_MAIN(app)       // emit the header for namespace `app`
```

```cmake
welder_generate_opaque_containers(app_opaque
  SOURCES app_opaque_gen.cpp
  OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/app.opaque.hpp)
# add_dependencies(your_module app_opaque) and put the OUTPUT dir on its include path
```

Then in the binding TU, include the generated header after your types and the backend
rod, before the module — the runtime path is unchanged:

```cpp
#include "app_types.hpp"
#include <welder/rods/python/pybind11/rod.hpp>
#include "app.opaque.hpp"                // GENERATED: WELDER_OPAQUE + aliases
PYBIND11_MODULE(app, m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^app>(m);
}
```

Every scalar-element container becomes opaque automatically. Two controls:

- **Opt a container out** with `[[=welder::rods::python::by_value]]` on a data member —
  its type keeps by-value (copy) binding. Because opaqueness is per-type, a `by_value`
  anywhere excludes that whole container type.
- **Names are derived** from the type: `std::vector<int>` → `VectorInt`,
  `std::map<std::string,int>` → `MapStringInt`.

Containers of **welded classes** work too — `std::vector<Entity>` where `Entity` is a
welded type is opened opaque, with clean stubs and live mutation, even when it is an
aggregate field or a function return. `weld_namespace` registers every welded type's
name *before* it binds any container or member, so no forward reference is ever spelled
as a raw C++ name (this "declare all names first" ordering also applies to hand-written
opaque aliases and to ordinary bindings).

!!! note "What the generator still leaves by value"

    Two element shapes aren't opened automatically: a **nested-in-a-class** welded type
    (`std::vector<Outer::Inner>` — its name is registered inside `Outer`'s body, too
    late), and a **nested container** (`std::map<K, std::vector<V>>`). Hand-write their
    `WELDER_OPAQUE` + alias if you want them opaque. Element types welded in a *different*
    `weld_namespace` also need that namespace welded first (the usual ordering).

## Scope & the trade-off

- **Supported containers** are exactly those the frameworks ship an opaque binder for:
  `std::vector` (sequence, via `bind_vector`) and `std::map` / `std::unordered_map`
  (mapping, via `bind_map`). `std::deque`, `std::list`, the sets and the `multi*`
  containers have no ready opaque binder and are not supported.
- **Opaqueness is per-type and module-wide.** `WELDER_OPAQUE(std::vector<int>)` makes
  *every* `std::vector<int>` in the module opaque — you cannot have one member copy and
  another reference for the same element type. A different element type
  (`std::vector<double>`) is independent, and stays a copied `list` unless you make it
  opaque too.
- **You lose transparent `list` interop for that type.** An opaque `std::vector<int>`
  parameter no longer auto-accepts a Python `list` — callers pass `IntVector([...])`.
  That is the inherent cost of reference semantics; weigh it per element type.
