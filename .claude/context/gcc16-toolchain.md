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

## Header-only for now, and the vocabulary boundary (important, gcc-16 specific)

**welder ships header-only.** The optional C++20 `import welder;` module wrapper
(`welder.cppm` + the `welder::module` target) was **removed** — it worked only in
gcc-16's experimental `-freflection` mode, and even there the module path conflicts
with a real pybind11 TU (below). No new compiler consumes it (Clang has only an
experimental P2996 fork; MSVC none). The user-facing writeup is
`docs/content/guide/header-only.md`. Reintroduce the wrapper once (a) a released
gcc-16 carries the PR123810 backport and PR99000 is resolved, and (b) Clang or MSVC
implements P2996 so a module is portable.

The **vocabulary/`<meta>` split is kept anyway** so the wrapper can return unchanged:
`lang.hpp` + `annotations.hpp` stay std-free (module-ready); reflection
(`reflect.hpp`) and rods are the `<meta>`-using headers. Why the split matters: on
gcc-16, any std header in a module unit's purview/GMF (even `<cstdint>`) makes every
consumer that both `import`s it and textually `#include`s std headers fail with
`conflicting imported declaration` errors (e.g. `std::wstreampos`/`__mbstate_t`) —
and `<meta>`/pybind11 include std textually. Partitioning doesn't help — it's
std-in-purview, not partitioning. Re-confirmed 2026-07-07 now that `import std;`
works: switching a std-carrying module to `import std;` in its purview does **not**
rescue the pybind consumer — it still conflicts under `-freflection` (the amplifier;
see gotchas). Only a genuinely **std-free** module survives a `-freflection` pybind
TU.

Consequently `reflect.hpp`/rods do **not** include the vocabulary headers (that would
redeclare them): provide the vocabulary first (`#include <welder/vocabulary.hpp>`),
then the rod header. welder also does **not** modularize internally (no partitions,
no per-component units) — header-only is the source of truth. Rods are always
header-only (e.g. `#include <welder/rods/python/pybind11/rod.hpp>`).

## Toolchain gotchas

- **`import std;` now works on this machine** — the empty-1-byte `bits/std.cc`
  bottle bug (Homebrew #289142) was fixed locally (upstream PR + patched brew
  formula; `bits/std.cc` is now ~113 KB, `std.gcm` builds to ~33 MB). Plain
  C++26 code can `import std;`. **But welder still uses textual std includes,**
  because welder compiles every TU with `-freflection` and includes a std-heavy
  backend (pybind11) — see the `-freflection` conflict below.
- **`-freflection` × `import std` × textual std includes conflict** (verified
  2026-07-07, `scratchpad/modtest` research project). A single TU that combines
  `-freflection`, a std-carrying `import` (either `import std;` directly, or an
  imported module whose interface/GMF pulls in std), *and* textual std includes
  (pybind11 / Python.h / macOS SDK `<arm/_types.h>`) fails with
  `conflicting imported declaration '__mbstate_t'` / `std::streampos` /
  `std::wstreampos`. Drop `-freflection` and the same code compiles (gcc merges
  BMI-std with textual-std cleanly — the import-std fix works there); keep it and
  the merge breaks on the foundational `__mbstate_t` typedef. Looks like a
  distinct gcc bug, separate from the (fixed) empty-`std.cc` bottle bug —
  worth an upstream report. Consequence: welder consumers/examples/tests use
  textual std includes, not `import std;`.
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
