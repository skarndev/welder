# welder

**welder** is a C++26 library that generates language bindings for annotated C++
types by reading **C++26 reflection** (P2996) and **annotations** (P3394) at
compile time. You mark a type with attributes describing *which languages* it
should be exposed to and *which members* participate; welder reflects over it and
emits the binding registration code (e.g. pybind11 `class_<T>` calls) directly —
no external code generator, no parsing step. Targets **C++26 and newer only**.

The unit that lays those bindings down for a given framework is a **rod** (a
welding rod): a stateless policy struct, `welder::rods::<name>::rod`. You drive it
through the one public entry point, `welder::welder<Rod>`, whose static members
automate the boring, backend-agnostic boilerplate at whichever stage of the usual
hand-binding flow you want — a single type (`weld_type`), a single free function or
global/constant (`weld_function` / `weld_variable`, the semi-manual route), a
namespace into an existing module (`weld_namespace`), a namespace as a fresh
submodule (`weld_namespace_as_submodule`), or a whole module (`weld_module`) —
leaving the rest ordinary hand-written binding code. `weld_type` / `weld_function` /
`weld_variable` / `weld_namespace_as_submodule` take an optional trailing `name`
override (used verbatim, beats `weld_as`). The actual traversal lives in an injectable
**carriage** (`welder::welder`'s defaulted third template arg): the default
`welder::stitch_welding_carriage` binds only where welder's markers direct, while
`welder::tack_welding_carriage` binds an **unmarked** third-party library greedily
(ignoring the missing `weld` markers, still enforcing bindability). Each `weld_*`
entry point is a one-line forward to the carriage, so a user can also subclass
`welder::welder` to compose bespoke routines from the same gated building blocks.

**Delivery model:** **header-only** (`src/welder/…`). The vocabulary arrives via
`#include <welder/vocabulary.hpp>`; rods pull in the core themselves (`#include
<welder/rods/python/pybind11/rod.hpp>` → `<welder/welder.hpp>`). The optional C++20
`import welder;` module wrapper was **removed** until the gcc-16 `-freflection`/
modules bugs are fixed and another toolchain (Clang/MSVC) implements P2996 — see
`docs/content/header-only.md` and `.claude/context/gcc16-toolchain.md`. The
vocabulary headers are still kept std-include-free so the wrapper can return
unchanged. We also deliberately do *not* modularize internally.

**Status:** early POC, verified end-to-end (an importable Python module; a
`require`-able Lua module). Four *runtime* rods are implemented —
two **Python** (**pybind11**, **nanobind**) and two **Lua** (**sol2**,
**LuaBridge3**) — all sharing the same core and the *same* backend-neutral C++ test
cases, which each rod binds and asserts (pytest for Python, busted `.lua` specs for
Lua) as a cross-rod consistency check. The two Lua rods run the *same* busted specs
(selected by `WELDER_TEST_LUA_MODULE`). Two more are *build-time* text-emitting rods
over the same driver: **`welder::rods::luacats::rod`** reflects the welded Lua types and
emits a **LuaCATS (`---@meta`) stub file** (the Lua analogue of the Python `.pyi` stubs,
carrying the docstrings Lua has no runtime slot for); **`welder::rods::trampolines::rod`**
reflects the welded *virtual* Python types and emits a **`.hpp` of ready-to-compile,
backend-neutral pybind11/nanobind trampoline subclasses** — so a Python subclass can
override their virtuals without the trampolines being hand-written (each override splices
the base virtual's reflected types, so signatures match by construction; a C-variadic
virtual with no `bind_flat` is a hard error). Further languages are designed-for but not
yet implemented. For the feature-by-feature detail and test locations, see the context
files below.

## The idea / public API

```cpp
#include <welder/vocabulary.hpp>  // annotation vocabulary (welder is header-only)
#include <pybind11/pybind11.h>
#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]  // expose to py+lua
       [[=welder::policy::automatic]]                          // reflect all members
ReflectedStruct {
    std::uint32_t first;                                              // bound everywhere
    [[=welder::mark::exclude]] std::uint32_t second;                  // bound nowhere
    [[=welder::mark::exclude(welder::lang::lua)]] std::string third;  // py, not lua
    [[=welder::mark::include(welder::lang::py)]] std::string last;    // opt-in (see policy)
};

PYBIND11_MODULE(mymod, m) {
    // name defaults to identifier_of(^^T)
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<ReflectedStruct>(m);
}
```

### Annotation vocabulary (`welder::`)

| Annotation | Meaning |
|---|---|
| `weld(lang...)` | Languages this type is exposed to. Required to bind. |
| `policy::automatic` | (default) Greedy: reflect every member unless excluded. |
| `policy::opt_in` | Conservative: bind only members marked `include`. |
| `mark::exclude` / `mark::exclude(lang...)` | Exclude member from all / the listed languages. |
| `mark::include` / `mark::include(lang...)` | Opt a member in (meaningful under `policy::opt_in`). |
| `mark::trust_bindable` / `mark::trust_bindable(lang...)` | Vouch that this member's type (or a callable's whole signature) is representable outside welder's view (e.g. hand-registered with pybind11); suppresses the bindability gate. |
| `trust_bindable<T> = true` | Type-level form: trust `T` everywhere it appears. A specializable `bool` variable template, not an attribute. |
| `doc("text")` | Docstring for a class, namespace, function, function parameter, or data member. Surfaced as `__doc__` by the Python rods (a data member's rides on its property; const → read-only); ignored on namespace variables. Lua has no runtime docstring slot, so the sol2 rod ignores it at runtime — its home there is the generated **LuaCATS (`---@meta`) stub** (`welder::rods::luacats::rod`). |
| `returns("text")` | Documents a function's return value (a `Returns:` block). Distinct from the summary `doc`. |
| `tparam("T", "text")` | Documents a template parameter (repeatable, ordered). Rides on the template itself; becomes `@tparam` in C++ docs, read back via `tparam_docs<Ent>()`. |
| `weld_as([lang…,] "name")` | Force this entity's target-language name **verbatim** (bypasses the name style). The name is last; zero or more `lang` markers precede it — none = all languages, one or several scope it. Repeat the annotation for a different name per language. |

`policy::auto` from the original sketch is spelled `policy::automatic` (`auto` is
reserved). Resolution per language `L` (`reflect.hpp` `member_bound`): excluded for
`L` → false; else `automatic` → true; else (`opt_in`) → true iff explicitly
included for `L`. A `lang` is a bit in an `unsigned` mask; mask `0` on an
exclude/include spec is the sentinel for "all languages".

## Conventions (always)

- Pure standard C++26 — **no gcc-only constructs** in library code. If a gcc
  workaround is unavoidable, isolate and comment it.
- **Vocabulary headers (`lang.hpp`, `annotations.hpp`) must stay std-include-free**
  so a future `import welder;` module wrapper can re-export them safely. Anything
  needing `<meta>`/std stays in `reflect.hpp`/rods. (This is the vocabulary-vs-`<meta>`
  boundary — details in `.claude/context/gcc16-toolchain.md`.)
- **Every welder namespace opens inside the ABI inline namespace** —
  `namespace welder::inline v0 { … }` (ODR guard for mixed-welder-version links).
  `src/welder/version.hpp` is the version's single source of truth
  (`WELDER_VERSION_*`; CMake + conanfile.py parse it) and defines
  `WELDER_ABI_NAMESPACE`; bump the namespace only on ABI-breaking releases. The
  Doxygen filter strips the token for the docs (see the architecture file map).
- Keep the core rod-agnostic. New languages are new rods under
  `src/welder/rods/`, each with its own `welder::<framework>` CMake target
  (e.g. `welder::pybind11`) exposing one `welder::rods::<name>::rod` struct.
- Prefer **brace initialization** (`int n{0};`).
- We value API/design quality over speed; write throwaway probes and *compile
  them* to validate reflection behavior before building on it.
- **Work on `main`** — commit directly, no feature branches for now.
- **Keep docs in sync with features:** any new/changed feature updates the guide
  (`docs/content/`), the C++ reference (doc comments), *and* the relevant
  `.claude/context/*` file in the same change.

## Context reference (read on demand)

CLAUDE.md stays lean; the detail lives in `.claude/context/`. Pull the matching
file into context when you touch that area — don't re-derive from the code.

| When you're… | Read |
|---|---|
| Planning/changing architecture, adding a rod (backend), or need the file/dir map or rod-interface contract | `.claude/context/architecture.md` |
| Hitting a C++20-module/`import std` error, a reflection API question, or any toolchain gotcha | `.claude/context/gcc16-toolchain.md` |
| Working on what binds — members, ctors, operators, enums, inheritance, namespaces, whole modules, templates | `.claude/context/binding-features.md` |
| Touching the bindability gate, the `trust_bindable` hatches, or pybind11 `type_caster` interplay | `.claude/context/bindability-gate.md` |
| Working on docstrings, the Doxygen INPUT_FILTER, or the docs site | `.claude/context/docs-and-doxygen.md` |
| Building, running the examples, or working on tests / `.pyi` stubs | `.claude/context/build-test-run.md` |

The user-facing narrative guide (`docs/content/guide/*.md`) has walkthroughs of
each feature; the `.claude/context/*` files are the developer/impl companions
(driver hooks, file & test locations, caveats).
