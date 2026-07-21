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
shows up as `append`, and a scalar vector exposes its raw buffer to NumPy **zero-copy**.

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
