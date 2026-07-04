# gcc-16 toolchain, modules & reflection

Read when: hitting a C++20-module or `import std` error, a reflection API
question, or any toolchain gotcha on this machine.

C++26 reflection is bleeding-edge; **gcc-16 is currently the only compiler that
implements P2996 + P3394**. welder is written against the *standard*, not gcc
extensions, so MSVC/Clang can be added once they catch up.

- Compiler: `g++-16` (Homebrew GCC 16.1.0) — `/opt/homebrew/bin/g++-16`
- Build: CMake (≥3.28 for `FILE_SET CXX_MODULES`) + **Ninja** (modules need it)
- Packages: Conan 2 (`conanfile.py`) → pybind11
- Python: Homebrew python3 (for the pybind11 module + Python.h)

## The module-vs-header boundary (important, gcc-16 specific)

The **`welder` module exports only the std-free vocabulary** (`lang`,
`annotations`); reflection (`reflect.hpp`) and backends are header-only and **not**
part of the module. Why: on gcc-16, any std header in a module unit's purview/GMF
(even `<cstdint>`) makes every consumer that both `import`s it and textually
`#include`s std headers fail with `conflicting imported declaration` errors (e.g.
`std::wstreampos`/`__mbstate_t`) — and `<meta>`/pybind11 include std textually. So
vocabulary stays std-free; anything touching `<meta>`/pybind11 stays a header.
Partitioning doesn't help — it's std-in-purview, not partitioning. (Empirical;
revisit if gcc fixes module/std merging or pybind11 becomes importable.)
Consequently `reflect.hpp`/backends do **not** include the vocabulary headers
(that would redeclare what `import welder;` provides): provide the vocabulary first
(`import welder;` *or* `#include <welder/welder.hpp>`), then the backend header.

This is also why welder does **not** modularize internally (no partitions, no
per-component units): header-only is the source of truth and the fallback. The core
has two equivalent forms — `import welder;` *or* `#include <welder/welder.hpp>`;
backends are always header-only (e.g. `#include <welder/backends/python/pybind11/backend.hpp>`).

## Toolchain gotchas

- **`import std;` is broken** on this Homebrew gcc-16 bottle (ships empty 1-byte
  `bits/std.cc` — Homebrew issue #289142). welder does not use it; we use textual
  includes. Deferred.
- pybind11 2.13's `pybind11_add_module` CMake helper is incompatible with the very
  new CMake/FindPython here; examples use CMake-native `Python_add_library` +
  `pybind11::headers` instead.

## Reflection cheatsheet (gcc-16)

- Flag: `-std=c++26 -freflection` (annotations included; no separate flag).
- Header `<meta>`, namespace `std::meta`. Reflect with `^^Thing`; splice `[: r :]`;
  pointer-to-member with `&[:member:]`.
- Read annotations: `annotations_of_with_type(entity, ^^Spec)` → `vector<info>`;
  value via `meta::extract<Spec>(info)`.
- **Gotcha:** query results are `std::vector<info>` and cannot live in a
  `constexpr` variable (non-transient allocation). Consume them inside a
  `consteval` helper, or promote with `std::define_static_array(...)` for
  `template for` loops. Use `std::define_static_string(...)` to get a runtime
  `const char*` from a name.
- `identifier_of` **throws** for non-identifier reflections (aliases like
  `uint32_t`, specializations like `string_view`); use `display_string_of` for
  type names.
- Doc/annotation text must be stored *inline* (`fixed_string`) — a `const char*` to
  a literal isn't a permitted annotation constant on gcc-16.
