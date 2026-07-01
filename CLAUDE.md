# welder

**welder** is a C++26 library that generates language bindings for annotated C++
types by reading **C++26 reflection** (P2996) and **annotations** (P3394) at
compile time. You mark a type with attributes describing *which languages* it
should be exposed to and *which members* participate; welder reflects over it and
emits the backend registration code (e.g. pybind11 `class_<T>` calls) directly —
no external code generator, no parsing step.

The project targets **C++26 and newer only**.

## Delivery model (Boost-style)

welder is fundamentally **header-only** (`src/welder/…`), with **one** optional
C++20 module wrapper (`src/welder/welder.cppm`) so users can `import welder;`
instead of including headers. Everything lives under a single tree — no separate
`include/`, so user includes start from `welder/`. We deliberately do **not**
modularize internally (no partitions, no per-component units): C++20 modules on
gcc-16 are too fragile (see "Module-vs-header boundary" below), so header-only is
the source of truth and the fallback. The core thus has two equivalent forms —
`import welder;` *or* `#include <welder/welder.hpp>` — and backends are always
header-only (e.g. `#include <welder/backends/pybind11.hpp>`).

## Status

Early POC, verified end-to-end (both consumption forms → an importable Python
module):

- Annotation vocabulary (`weld`, `policy`, `mark`, `doc`, `returns`) +
  compile-time resolution of which members bind per language (`reflect.hpp`:
  `welded_for && member_bound`).
- **pybind11 backend** (`backends/pybind11.hpp`), honoring exclude/include/policy:
  - public data members (read/write); constructors (default + each public
    non-copy/move ctor → `pybind11::init<...>`; plus, for a baseless **aggregate**,
    a synthesized field constructor that brace-inits it, giving Python
    `T(f0, f1, …)` — only when every field binds, since aggregate init is
    positional/all-or-nothing); methods, static methods, overloads. Function /
    method / constructor **parameter names** reach Python as keyword arguments
    (`py::arg`) when every parameter of that signature is named.
  - **overloaded operators** → Python special methods. A *member* operator binds
    under its dunder (`operator+` → `__add__`, `operator==` → `__eq__`,
    `operator[]` → `__getitem__`, `operator()` → `__call__`, …), unary vs binary
    told apart by arity so the two `operator-` forms map to `__neg__` / `__sub__`.
    Arithmetic / bitwise / comparison / call / subscript are covered; in-place
    compound assignment (`operator+=`) is intentionally not mapped (Python falls
    back to `a = a + b` via `__add__`), nor are `<=>`, `&&`, `||`, `++`, `--`,
    `operator=` (special member). *Free* (non-member) operators aren't bound yet.
  - **bindability gate ("pybind11-convertible").** Every surface welder is about to
    bind (data member, parameter, return type, namespace variable) must be a type
    pybind11 can convert to a *meaningful* Python value; otherwise it is a **hard
    compile error** (`static_assert` in `assert_bindable`, naming the offending
    type) — never a silent skip, since binding such a type yields a dead attribute
    *and* a stub referencing an unimportable type (breaking pybind11-stubgen). The
    fix in the message: weld the type, give it a pybind11 `type_caster`, or
    `mark::exclude` the member. Mechanism (backend-specific, in `pybind11.hpp`):
    `needs_registration<T>` — is T's caster the generic `type_caster_base` fallback
    (a class/enum needing a registered `class_`/`enum_`) vs. a specialized/native
    caster? — plus `wrapper_traits<T>`, which recurses the value types of the STL
    containers, `optional`, `pair`/`tuple`/`variant` and the smart-pointer holders
    (so `std::vector<Unwelded>` is caught, not just a bare `Unwelded`). `bindable<T,
    L>()` folds these: a wrapper binds iff its elements do; a registration-needing
    class/enum binds iff `welded_for` (a user `type_caster` flips it native, so it's
    trusted automatically). *Not* exhaustive for a non-STL wrapper with its own
    caster — its elements aren't recursed (treated as an opaque bindable leaf).
    Negative-compile cases live in `tests/pybind11/cpp/neg/` (`negcompile.*` CTests,
    `WILL_FAIL`). Two deferred escape hatches for types welder can't see are welded
    (e.g. hand-registered with pybind11): a `mark::trust_bindable` opt-out, and a
    backend-agnostic `welder::bindable_as<T>` customization point that also feeds
    stub type names — both TODO.
  - **inheritance from public bases.** `weld` is a *discovery marker* (an
    independently-registered, module-discoverable entity), not an inheritance
    directive: the most-derived type's `weld` drives which languages bind, and a
    base need not be welded. A **welded** base → a native pybind11 base
    (`class_<T, Base...>`; bind it separately, first), including the nearest welded
    ancestors reached *through* non-welded ones (deduplicated). A **non-welded**
    base → a C++ mixin whose eligible members are flattened in recursively
    (honoring its own marks/policy). Virtual diamonds work; a non-virtual diamond
    with a shared welded base is a C++ ambiguity (not worked around).
  - **whole-namespace binding** — `bind_namespace<^^ns>(m)`. `weld` gates *leaf
    entities only* (class type / free function / namespace-scope variable;
    namespaces are never welded); the namespace `policy` (default automatic) +
    member marks then resolve. Binds classes (`bind<T>`), free functions (overloads
    included), and namespace variables as module attributes — a **value snapshot if
    const/constexpr, else a live get/set property** over the C++ global (via a
    `ModuleType` `__class__` swap). A **nested namespace** resolves under the
    *parent's* policy (no weld; automatic recurses unless excluded, opt_in only if
    included — keeps `detail`/`impl` out) and becomes a submodule when it holds
    bound content. Declaration order.
  - **docstrings** (`doc.hpp`, backend-agnostic) — `[[=welder::doc("…")]]` on a
    class/namespace/function/parameter, `[[=welder::returns("…")]]` on a function.
    A return value isn't a reflectable entity, so its doc rides on the function as
    a *distinct* spec type (`return_doc_spec`), told apart from the summary by spec
    type. `function_docstring<^^Fn, Style>()` folds summary + param docs + return
    doc (via a `function_doc` parts struct, extensible to future `Raises:`/`Note:`
    without re-breaking the style API) under a pluggable style (default
    `google_style` → `Args:`/`Returns:` blocks); surfaced as Python `__doc__`.
    Variable docs are intentionally ignored. Doc text is stored *inline*
    (`fixed_string`) — a `const char*` to a literal isn't a permitted annotation
    constant on gcc-16.
  - **whole-module binding** — `build_module<^^ns>(m, pre, post)` fills an
    *existing* module (pre hook → `bind_namespace` → post hook; namespace `doc` →
    module doc). The C entry symbol `PyInit_<name>` must be preprocessor-pasted, so
    the backend-agnostic `WELDER_MODULE(ns, backend)` macro (`module.hpp`) wraps it
    (namespace token = module name, optional trailing `{ }` post-glue with the
    module handle in scope as `module`). One `WELDER_MODULE` per backend per TU;
    two Python backends collide (both emit `PyInit_<name>`).
  - **`.pyi` stub generation** via [pybind11-stubgen](https://github.com/pybind/pybind11-stubgen)
    (build-time): `cmake/WelderPybind11Stubgen.cmake` → `welder_pybind11_generate_stubs(<target>
    PYTHON <interp> …)`, a POST_BUILD step (`--exit-code`); gated by
    `WELDER_BUILD_STUBS` (default ON). `PYTHON` must import the extension (ABI
    match) and have stubgen; welder docstrings flow into the stubs. Three test-side
    mypy gates — `stubcheck` (mypy over each stub tree), `typingcases`
    (pytest-mypy-testing cases in `tests/test_types.mypy-testing` against the
    backend-neutral canonical name `welder_test` on `MYPYPATH`), `mypy.tests`
    (plain mypy over the `.py` specs, which are `Any` to mypy via the `ModuleType`
    fixture). Examples opt in via `-DWELDER_STUBGEN_PYTHON=<interp>`.
    pybind11-stubgen is pinned to its GitHub `main` branch (fixes not yet on PyPI;
    see `tests/pyproject.toml` `[tool.uv.sources]`).

Enums, properties, custom type converters, and additional languages (Lua, …) are
designed-for but **not yet implemented**.

## The idea / public API

```cpp
import welder;            // or: #include <welder/welder.hpp>
#include <pybind11/pybind11.h>
#include <welder/backends/pybind11.hpp>

struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]  // expose to py+lua
       [[=welder::policy::automatic]]                          // reflect all members
ReflectedStruct {
    std::uint32_t first;                                              // bound everywhere
    [[=welder::mark::exclude]] std::uint32_t second;                  // bound nowhere
    [[=welder::mark::exclude(welder::lang::lua)]] std::string third;  // py, not lua
    [[=welder::mark::include(welder::lang::py)]] std::string last;    // opt-in (see policy)
};

PYBIND11_MODULE(mymod, m) {
    welder::pybind11::bind<ReflectedStruct>(m);  // name defaults to identifier_of(^^T)
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
| `doc("text")` | Docstring for a class, namespace, function, or function parameter. Surfaced as the target language's `__doc__`; ignored on variables. |
| `returns("text")` | Documents a function's return value (a `Returns:` block in its docstring). Distinct from the function's summary `doc`. |

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

`src/` is the include root, so every public include starts from `welder/`.

```
src/welder/
  detail/config.hpp     WELDER_EXPORT macro (export under the module, else empty)
  lang.hpp              enum class lang                       — std-free vocabulary
  annotations.hpp       weld / policy / mark / doc + mask helpers — std-free vocabulary
  reflect.hpp           welded_for / policy_of / member_bound / public_bases — uses <meta>
  doc.hpp               doc_of / return_doc_of / param_docs / doc styles / function_docstring — uses <meta>
  module.hpp            WELDER_MODULE(ns, backend) entry-point dispatch macro
  welder.hpp            header-only umbrella: lang+annotations+reflect+doc
  welder.cppm           the single `export module welder;` (exports vocabulary only)
  backends/
    pybind11.hpp        pybind11 backend: bind<T> / bind_namespace / build_module
    CMakeLists.txt      target: welder::pybind11  (nanobind / lua planned here)
src/CMakeLists.txt      targets: welder::headers / welder::module
cmake/
  WelderPybind11Stubgen.cmake  welder_pybind11_generate_stubs() — .pyi via pybind11-stubgen
examples/
  python_poc/             consumes `import welder;`
  python_poc_headeronly/  consumes welder header-only
  welder_module/          whole-module binding via WELDER_MODULE
```

`doc.hpp` is part of the reflection layer (like `reflect.hpp`): it uses `<meta>`
and is header-only, so it is **not** part of the `welder` module and does not
include `annotations.hpp` (the vocabulary arrives first via `import welder;` or
`welder.hpp`). `module.hpp` is macro-only and backend-agnostic; each backend
header defines its `WELDER_DETAIL_MODULE_ENTRY_<backend>` expansion.

CMake targets:
- **`welder::headers`** — INTERFACE, the header-only core (include path + flags).
- **`welder::module`** — STATIC, builds `src/welder/welder.cppm`; provides `import welder;`.
- **`welder::pybind11`** — INTERFACE, the pybind11 backend (links headers + pybind11 + Python).
  Gated by `WELDER_BUILD_PYBIND11`. Future Python (nanobind) / Lua backends get
  their own `welder::<backend>` target alongside it.

### Module-vs-header boundary (important, gcc-16 specific)

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

**Backend namespace.** The pybind11 backend is `welder::pybind11` (nanobind →
`welder::nanobind`; both target `lang::py`). Inside it, unqualified `pybind11`
would resolve to that namespace, so the header aliases `namespace py = ::pybind11;`
once and uses `py::` throughout.

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
  `src/welder/backends/` (e.g. `lua.hpp`), each with its own `welder::<backend>`
  target in `src/welder/backends/CMakeLists.txt`.
- Prefer **brace initialization** (`int n{0};`) for variable initialization where
  possible.
- We value API/design quality over speed; write throwaway probes and *compile
  them* to validate reflection behavior before building on it.
