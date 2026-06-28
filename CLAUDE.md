# welder

**welder** is a C++26 library that generates language bindings for annotated C++
types by reading **C++26 reflection** (P2996) and **annotations** (P3394) at
compile time. You mark a type with attributes describing *which languages* it
should be exposed to and *which members* participate; welder reflects over it and
emits the backend registration code (e.g. pybind11 `class_<T>` calls) directly —
no external code generator, no parsing step.

The project targets **C++26 and newer only**.

## Delivery model (Boost-style)

welder is fundamentally a **header-only** library (`include/welder/…`). On top of
that it ships **one** optional C++20 module wrapper (`modules/welder.cppm`) so
users can write `import welder;` instead of including headers.

We deliberately do **not** modularize internally (no partitions, no per-component
module units). C++20 modules on the only currently-viable toolchain (gcc-16) are
too fragile — see "Module-vs-header boundary" below. Header-only is the source of
truth and the fallback when a compiler's modules break.

So there are two equivalent ways to consume the core:

```cpp
import welder;                  // module form
// — or —
#include <welder/welder.hpp>    // header-only form
```

Backends are always header-only (e.g. `#include <welder/python.hpp>`).

## Status

Early development / proof-of-concept. Working today (verified end-to-end, both
consumption forms, producing an importable Python module):

- Annotation vocabulary (`weld`, `policy`, `mark::exclude`, `mark::include`).
- Compile-time resolution of which members bind for a given language.
- pybind11 backend binding public data members of a struct, honoring exclusion.

Methods, inheritance, enums, custom type converters, and additional languages
(Lua, …) are designed-for but **not yet implemented**.

## The idea / public API

```cpp
import welder;            // or: #include <welder/welder.hpp>
#include <pybind11/pybind11.h>
#include <welder/python.hpp>

struct [[=welder::weld(welder::lang::py, welder::lang::lua)]] // expose to py+lua
       [[=welder::policy::automatic]]                         // reflect all members
ReflectedStruct {
    std::uint32_t first;                              // bound everywhere

    [[=welder::mark::exclude]] std::uint32_t second;  // bound nowhere

    [[=welder::mark::exclude(welder::lang::lua)]]
    std::string third;                                // bound in py, not lua

    [[=welder::mark::include(welder::lang::py)]]
    std::string last;                                 // opt-in (see policy)
};

PYBIND11_MODULE(mymod, m) {
    welder::py::bind<ReflectedStruct>(m); // name defaults to identifier_of(^^T)
}
```

### Annotation vocabulary (`welder::`)

| Annotation | Meaning |
|---|---|
| `weld(lang...)` | Languages this type is exposed to. Required to bind. |
| `policy::automatic` | (default) Greedy: reflect every member unless excluded. |
| `policy::opt_in` | Conservative: bind only members marked `include`. |
| `mark::exclude` | Exclude member from **all** welded languages. |
| `mark::exclude(lang...)` | Exclude member from the listed languages only. |
| `mark::include` / `mark::include(lang...)` | Opt a member in (meaningful under `policy::opt_in`). |

**Naming deviation from the original sketch:** the sketch used
`welder::policy::auto`, but `auto` is a reserved keyword, so welder spells it
`welder::policy::automatic`. Under `policy::automatic`, an `include` mark is
redundant; emitting a compile-time diagnostic for that is a TODO.

Resolution rule (per language `L`), in `<welder/reflect.hpp>` —
`member_bound(member, L, policy)`:
- excluded for `L` → **false**
- else `automatic` → **true**
- else (`opt_in`) → **true iff** explicitly included for `L`.

A `lang` is stored as a bit in an `unsigned` mask; mask `0` on an exclude/include
spec is the sentinel for "all languages".

## Architecture

Language-agnostic **core** + pluggable **backends**. Adding a language = adding a
backend header that consumes the same resolution API; the core never depends on a
backend.

```
include/welder/
  detail/config.hpp   WELDER_EXPORT macro (export under the module, else empty)
  lang.hpp            enum class lang                       — std-free vocabulary
  annotations.hpp     weld / policy / mark + mask helpers   — std-free vocabulary
  reflect.hpp         welded_for / policy_of / member_bound — uses <meta>
  welder.hpp          header-only umbrella: lang+annotations+reflect
  python.hpp          pybind11 backend: welder::py::bind<T>
modules/
  welder.cppm         the single `export module welder;` (exports vocabulary only)
src/CMakeLists.txt    targets: welder::headers / welder::module / welder::python
examples/
  python_poc/             consumes `import welder;`
  python_poc_headeronly/  consumes welder header-only
```

CMake targets:
- **`welder::headers`** — INTERFACE, the header-only core (include path + flags).
- **`welder::module`** — STATIC, builds `modules/welder.cppm`; provides `import welder;`.
- **`welder::python`** — INTERFACE, the pybind11 backend (links headers + pybind11 + Python).

### Module-vs-header boundary (important, gcc-16 specific)

The **`welder` module exports only the std-free vocabulary** (`lang`,
`annotations`). Reflection (`reflect.hpp`) and backends (`python.hpp`) are
header-only and are **not** part of the module.

Why: on gcc-16, if a module unit pulls *any* standard-library header into its
purview/GMF (even `<cstdint>`), every consumer that both `import`s it and
textually `#include`s those std headers gets hard errors like
`conflicting imported declaration 'std::wstreampos'` / `__mbstate_t`. pybind11
(and `<meta>`) include the standard library textually, so this fires for any real
backend TU. Hence: vocabulary modules stay std-free; everything touching `<meta>`
or pybind11 stays a header. Flattening partitions does **not** help — it is the
std-in-module-purview that conflicts, not partitioning. (Empirically established;
revisit if gcc fixes module/std merging or pybind11 becomes importable.)

`reflect.hpp`/`python.hpp` therefore do **not** include the vocabulary headers
(that would redeclare what `import welder;` already provides). Provide the
vocabulary first — `import welder;` *or* `#include <welder/welder.hpp>` — then the
backend header.

Complex/custom type conversions are intended to be registered per-backend via
pybind11's own mechanisms, separately from core resolution — design pending.

## Toolchain (this machine)

C++26 reflection is bleeding-edge; **gcc-16 is currently the only compiler that
implements P2996 + P3394**. welder is written against the *standard*, not gcc
extensions, so MSVC/Clang can be added once they catch up. Reflection/module
flags are isolated in the `welder_flags` INTERFACE target and gated on compiler
id, so nothing gcc-specific leaks into the public targets.

- Compiler: `g++-16` (Homebrew GCC 16.1.0) — `/opt/homebrew/bin/g++-16`
- Build: CMake (≥3.28 for `FILE_SET CXX_MODULES`) + **Ninja** (modules need it)
- Packages: Conan 2 (`conanfile.py`) → pybind11
- Python: Homebrew python3 (for the pybind11 module + Python.h)

Known toolchain gotchas:
- **`import std;` is broken** on this Homebrew gcc-16 bottle (ships empty 1-byte
  `bits/std.cc` — Homebrew issue #289142). welder does not use it; we use textual
  includes. Deferred.
- pybind11 2.13's `pybind11_add_module` CMake helper is incompatible with the very
  new CMake/FindPython here; examples use CMake-native `Python_add_library` +
  `pybind11::headers` instead.

### Reflection cheatsheet (gcc-16)

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

## Build & run

```bash
conan install . -pr:a conan/profiles/gcc16 --build=missing
cmake --preset welder-gcc16
cmake --build --preset welder-gcc16
```

Then both example Python modules are importable:

```bash
PYTHONPATH=build/welder-gcc16/examples/python_poc \
  python3 -c "import welder_poc as w; p=w.Point(); p.x=1.5; print(p.x)"
PYTHONPATH=build/welder-gcc16/examples/python_poc_headeronly \
  python3 -c "import welder_poc_ho as w; print(hasattr(w.Label(), 'cache'))"  # False
```

## Conventions

- Pure standard C++26 — **no gcc-only constructs** in library code. If a gcc
  workaround is unavoidable, isolate and comment it.
- **Vocabulary headers (`lang.hpp`, `annotations.hpp`) must stay std-include-free**
  so the module can export them safely. Anything needing `<meta>`/std stays in
  `reflect.hpp`/backends.
- Keep the core backend-agnostic. New languages are new headers under
  `include/welder/` (e.g. `lua.hpp`).
- We value API/design quality over speed; write throwaway probes and *compile
  them* to validate reflection behavior before building on it.
