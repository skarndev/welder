# welder

**welder** is a C++26 library that generates language bindings for annotated C++
types by reading **C++26 reflection** (P2996) and **annotations** (P3394) at
compile time. You mark a type with attributes describing *which languages* it
should be exposed to and *which members* participate; welder reflects over it and
emits the backend registration code (e.g. pybind11 `class_<T>` calls) directly —
no external code generator, no parsing step.

The project targets **C++26 and newer only**.

## Delivery model (Boost-style)

welder is fundamentally a **header-only** library (`src/welder/…`). On top of
that it ships **one** optional C++20 module wrapper (`src/welder/welder.cppm`) so
users can write `import welder;` instead of including headers.

Everything lives under a single tree (`src/welder/`) — there is no separate
`include/` directory — so user includes always start from `welder/`.

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

Backends are always header-only (e.g. `#include <welder/backends/pybind11.hpp>`).

## Status

Early development / proof-of-concept. Working today (verified end-to-end, both
consumption forms, producing an importable Python module):

- Annotation vocabulary (`weld`, `policy`, `mark::exclude`, `mark::include`).
- Compile-time resolution of which members bind for a given language.
- pybind11 backend binding, honoring exclude/include/policy:
  - public data members (read/write);
  - constructors (default + each public non-copy/move constructor, with
    parameter types reflected into `pybind11::init<...>`);
  - methods and static methods, including overloads.
  - inheritance from public bases. `weld` is a **discovery marker** ("this type
    is an independently-registered, module-discoverable entity"), not an
    inheritance directive: the *most-derived* type's `weld` drives which languages
    bind, and a base need not be welded to contribute members. From that:
    a **welded** base — registered as its own Python class — becomes a native
    pybind11 base (`class_<T, Base...>`, real subclass, inherited members via the
    MRO; bind it separately, before T), including welded bases reached only
    *through* non-welded ones (nearest welded ancestors, deduplicated). A
    **non-welded** base is a plain C++ mixin whose eligible members are flattened
    into each derived binding (recursively, honoring the base's own marks/policy).
    Virtual diamonds are supported and tested; non-virtual diamonds with a shared
    welded base are a C++ ambiguity (not worked around).
  - whole-namespace introspection via `welder::pybind11::bind_namespace<^^ns>(m)`. `weld`
    is the discovery gate for **leaf entities only** — a class type / free function
    / namespace-scope variable is a *candidate* iff welded (namespaces are never
    welded). The namespace's `policy` (default automatic) and the member's
    exclude/include marks then resolve what binds, mirroring struct member
    resolution (`welded_for && member_bound`). Exposes class types (via `bind<T>`),
    free functions (overloads included), and namespace-scope variables as module
    attributes — a **value snapshot if const/constexpr, else a live get/set
    property** over the C++ global (installed by swapping the module's `__class__`
    for a `ModuleType` subclass carrying the property descriptors). A **nested
    namespace** is resolved like a leaf member under the *parent's* policy
    (`member_bound`, no weld): automatic recurses unless `mark::exclude`'d, opt_in
    recurses only if `mark::include`'d — letting you keep `namespace detail`/`impl`
    out. It becomes a submodule when it holds bound content (then resolved under its
    own policy). Members bind in declaration order, so a welded base precedes its
    derived types (C++ already requires that within a namespace).
  - docstrings via the `[[=welder::doc("text")]]` annotation on a class,
    namespace, function, or function parameter, plus `[[=welder::returns("text")]]`
    on a function to document its **return value**. A return value is not a
    reflectable entity, so its doc rides on the function as a *distinct* spec type
    (`return_doc_spec`) — the summary `doc` there is already taken; the reflection
    layer tells them apart by spec type. The reading/formatting layer
    (`src/welder/doc.hpp`) is backend-agnostic: `annotation_text_of<^^E, ^^Spec>()`
    is the generic reader behind `doc_of<^^E>()` and `return_doc_of<^^Fn>()`;
    `function_docstring<^^Fn, Style>()` folds a function's summary, parameter docs,
    and return doc (gathered into a `function_doc` parts struct — extensible to
    future `Raises:`/`Note:` sections without re-breaking the style API) into one
    docstring via a pluggable **style** (default `welder::google_style` — summary +
    Google `Args:` + `Returns:` blocks). The pybind11 backend surfaces them as
    Python `__doc__`: class/namespace docstrings verbatim, methods/free functions
    with their argument and return docs folded in. **Variable docs are
    intentionally ignored** (module attributes / `def_readwrite` properties have no
    natural `__doc__` slot in this model). The text is stored *inline*
    (`welder::fixed_string`) because a `const char*` to a string literal is not a
    permitted annotation constant on gcc-16.
  - whole-module binding from a namespace via `welder::pybind11::build_module<^^ns>(m, pre, post)`
    (a top-level namespace → a filled module: optional `pre` hook, `bind_namespace`,
    optional `post` hook; the namespace's `doc` becomes the module docstring). It is
    macro-free but fills an *existing* module — the module's C entry symbol
    (`PyInit_<name>`) must be pasted by the preprocessor, so it can't be synthesized
    from a reflection. The backend-agnostic **`WELDER_MODULE(ns, backend)`** macro
    (`src/welder/module.hpp`) hides that one irreducible macro: it expands to the
    selected backend's entry-point macro + a `build_module<^^ns>` call, with the
    namespace token doubling as the module name and an optional trailing `{ }` block
    of post-glue (the module handle in scope as `module`). Different *languages* can
    coexist in one TU (one `WELDER_MODULE` per backend — `PyInit_<name>` vs
    `luaopen_<name>` are distinct symbols); two Python backends (pybind11 +
    nanobind) cannot, as both emit `PyInit_<name>`.
  - Python type stubs (`.pyi`) generated from a built extension via
    [pybind11-stubgen](https://github.com/pybind/pybind11-stubgen). This is a
    *build-time* concern, not C++: the `welder_pybind11_generate_stubs(<target>
    PYTHON <interp> [MODULE …] [OUTPUT_DIR …] [ARGS …])` CMake helper
    (`cmake/WelderPybind11Stubgen.cmake`, `include`d from the root) attaches a
    POST_BUILD step that imports the freshly built module and emits stubs
    (`--exit-code`, so an unrepresentable stub fails the build). The interpreter
    passed in `PYTHON` must both *import the extension* (ABI match) and have
    pybind11-stubgen installed. The welder docstrings reflected into pybind11
    `__doc__` flow straight through into the stubs (Google `Args:` blocks and all).
    Gated by `WELDER_BUILD_STUBS` (default ON). Three layers of type-checking in
    the tests (see `welder-pybind11-stubgen` / `welder-test-harness` memories):
    `stubcheck.<variant>` runs `mypy --strict` over each generated tree (stubs
    well-formed?); `typingcases.pybind11` runs **pytest-mypy-testing** cases
    (`tests/test_types.mypy-testing`, backend-neutral — they import the canonical
    name `welder_test`, which the CTest puts on `MYPYPATH` pointing at the stubs)
    that assert revealed types (stubs correct in use?); and `mypy.tests` runs a
    plain strict mypy over the `.py` specs themselves. The runtime specs reach the
    module through a `ModuleType` fixture, so they're `Any` to mypy — the
    type-level cases are where the stubs get exercised. Examples generate stubs
    opt-in via `-DWELDER_STUBGEN_PYTHON=<interp>`. **pybind11-stubgen
    is currently sourced from its GitHub `main` branch** (the stub fixes welder
    relies on aren't on a PyPI release yet) — see `tests/pyproject.toml`
    `[tool.uv.sources]`.

Enums, properties, custom type converters, and additional languages (Lua, …)
are designed-for but **not yet implemented**.

## The idea / public API

```cpp
import welder;            // or: #include <welder/welder.hpp>
#include <pybind11/pybind11.h>
#include <welder/backends/pybind11.hpp>

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
    welder::pybind11::bind<ReflectedStruct>(m); // name defaults to identifier_of(^^T)
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
`annotations`). Reflection (`reflect.hpp`) and backends (`backends/pybind11.hpp`)
are header-only and are **not** part of the module.

Why: on gcc-16, if a module unit pulls *any* standard-library header into its
purview/GMF (even `<cstdint>`), every consumer that both `import`s it and
textually `#include`s those std headers gets hard errors like
`conflicting imported declaration 'std::wstreampos'` / `__mbstate_t`. pybind11
(and `<meta>`) include the standard library textually, so this fires for any real
backend TU. Hence: vocabulary modules stay std-free; everything touching `<meta>`
or pybind11 stays a header. Flattening partitions does **not** help — it is the
std-in-module-purview that conflicts, not partitioning. (Empirically established;
revisit if gcc fixes module/std merging or pybind11 becomes importable.)

`reflect.hpp`/`backends/pybind11.hpp` therefore do **not** include the vocabulary
headers (that would redeclare what `import welder;` already provides). Provide the
vocabulary first — `import welder;` *or* `#include <welder/welder.hpp>` — then the
backend header.

**Backend namespace.** The pybind11 backend lives in `welder::pybind11` (so a
future nanobind backend can be `welder::nanobind`; both target the `lang::py`
*language*). Inside `welder::pybind11`, the unqualified name `pybind11` would
resolve to *that* namespace rather than the library, so the header aliases the
real one once — `namespace py = ::pybind11;` — and uses `py::` throughout.

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
