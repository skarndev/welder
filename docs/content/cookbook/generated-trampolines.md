# 05 — Generated trampolines

*Source: [`examples/cookbook/05-generated-trampolines`][src].*

The trampolines in recipe [04](virtuals.md) are mechanical — name, signature,
one macro line per virtual. Mechanical means generatable: the
`welder::rods::trampolines` rod reflects the welded virtual types at **build
time** and emits the whole trampoline header, overrides and `trampoline_for`
registrations included. Each override *splices* the base virtual's reflected
return/parameter types, so the signatures match by construction.

## The three pieces

**The types** (`machines.hpp`) — just welded polymorphic classes; no trampolines,
no backend includes. The interesting shapes are covered: a plain base, a
*derived* welded type (its trampoline must also cover the **inherited**
virtuals), an abstract base, and a `bind_flat` method the generator must skip.

**The generator** (`gen.cpp`) — a three-line build-time executable:

```cpp
#include <welder/vocabulary.hpp>
#include <welder/rods/python/trampolines/module.hpp>
#include "machines.hpp"

WELDER_TRAMPOLINES_MAIN(machines)
```

**The wiring** (`CMakeLists.txt`) — `welder_generate_trampolines()` builds the
generator, runs it into `machines.trampolines.hpp`, and the binding TU compiles
the result:

```cmake
welder_generate_trampolines(machines_trampolines
  SOURCES gen.cpp
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/machines.trampolines.hpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/machines.hpp)

target_include_directories(machines PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(machines machines_trampolines)
```

The binding TU then differs from recipe 04 by exactly one include:

```cpp
#include "machines.hpp"             // the welded virtual types
#include "machines.trampolines.hpp" // GENERATED — replaces the hand-written structs
```

The generated header is **backend-neutral** (it uses the same neutral macros), so
one header serves the pybind11 and nanobind rods alike.

## What the check asserts

Byte-for-byte the same behaviors as recipe 04's hand-written trampolines: C++
dispatches into Python overrides (including an *inherited* virtual overridden on
the derived type), `bind_flat` stays flat, and the abstract base is
implementable from Python.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/05-generated-trampolines