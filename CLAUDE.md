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
(selected by `WELDER_TEST_LUA_MODULE`). Three more are *build-time* text-emitting rods
over the same driver: **`welder::rods::luacats::rod`** reflects the welded Lua types and
emits a **LuaCATS (`---@meta`) stub file** (the Lua analogue of the Python `.pyi` stubs,
carrying the docstrings Lua has no runtime slot for); **`welder::rods::trampolines::rod`**
reflects the welded *virtual* Python types and emits a **`.hpp` of ready-to-compile,
backend-neutral pybind11/nanobind trampoline subclasses** — so a Python subclass can
override their virtuals without the trampolines being hand-written (each override splices
the base virtual's reflected types, so signatures match by construction — overloaded
virtuals dispatch per-slot, covariant overrides fold to one slot, protected NVI hooks
are covered; a C-variadic virtual with no `bind_flat` is a hard error); and
**`welder::rods::opaque_containers::rod`** reflects the welded types, finds the STL
containers they use (`std::vector<Entity>` of a welded class included, not just
scalars), and emits a **`.hpp` of `WELDER_OPAQUE` declarations + welded aliases** that
bind them by reference — so the per-container boilerplate is not hand-written (a
namespace-scope `type_caster` specialization is a compile-time artifact no runtime rod
can emit; blanket over welded types, `by_value` opt-out, **collision-free
namespace-qualified derived names** — `vector<geo::Point>`→`VectorGeoPoint` — overridable
per-type via an optional `transform_opaque_container(enclosing, container, member)` hook on
the name style). Class-element
containers are made ordering-safe by the driver's **two-phase namespace sweep** (the
Python rods opt in via a `reopen_class` hook): it registers every welded type's NAME,
then binds the opaque containers, then fills members — so no container-typed
member/signature ever spells a raw C++ name in a stub. Further languages are
designed-for but not yet implemented. There is also a **cookbook** (`examples/cookbook` + the docs Cookbook
section): a *standalone* super-project of 9 CTest-asserted recipes that obtains welder
via FetchContent — CI builds it against the checkout, so it doubles as the consumer-
packaging test (details in `.claude/context/build-test-run.md`). For the
feature-by-feature detail and test locations, see the context files below.

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
| `policy::weld_protected` / `policy::weld_protected(lang...)` | Admit the type's **protected** members into resolution (they then resolve like public ones — policy kind, marks, groups, gate). A separate annotation, combinable with `automatic`/`opt_in`; repeats union; read through template instantiations. **Private is never admitted** (hard-wired before any hook). Protected **constructors** stay unbound (no ctor PM; deferred — a protected *default* ctor still constructs via a trampoline). Third-party/tack route: `greedy_resolution<true>` (whole-pass knob) or a custom resolution's optional `protected_participates(mem, L)` hook. |
| `mark::exclude` / `mark::exclude(lang...)` | Exclude member from all / the listed languages. |
| `mark::include` / `mark::include(lang...)` | Opt a member in (meaningful under `policy::opt_in`). |
| `mark::only(lang...)` | The **complete** set of languages this member may bind for — closed-world counterpart of `exclude`; under `opt_in` it is also the opt-in. Must be called with ≥ 1 lang (bare form diagnosed); `exclude` still beats it; repeats union. |
| `mark::no_reassign` / `mark::no_reassign(lang...)` | Force a **data member**'s read-only *binding* independent of its C++ const-ness: the object may still be mutated **in place** (the read-only binding hands out a live reference — `obj.items.append(x)` writes through) but the attribute can't be **rebound** to a different object (`obj.items = [...]` rejected). Exactly a const member's binding, without the const (pybind11 `def_readonly` / nanobind `def_ro` / sol2 `sol::readonly` / LuaBridge3 getter-only `addProperty` / luacats `(read-only)` note). Motivating case: a mutable STL container member. Bare = all languages, scope by lang; repeats union; a no-op on an already-const member but a **hard error anywhere but a nonstatic data member** (function, type, static member, nested type, global — diagnosed at every sweep + manual entry point via `has_no_reassign_mark`). |
| `mark::trust_bindable` / `mark::trust_bindable(lang...)` | Vouch that this member's type (or a callable's whole signature) is representable outside welder's view (e.g. hand-registered with pybind11); suppresses the bindability gate. |
| `getter` / `getter([lang…,] "name")` | Bind this **const, 0-param** member function as a property read (alone = read-only property); the marked function stops being a method for the covered languages (elsewhere it stays one). Name = explicit (verbatim, the property's `weld_as` — a real `weld_as` on an accessor is diagnosed) else the identifier styled-then-stripped of a leading `get`/`set` word (`is_` never stripped). Under `opt_in` the mark is also the opt-in. Static/virtual accessors and free-function marks are designed errors. |
| `setter` / `setter([lang…,] "name")` | The property write half (exactly 1 param), paired with a getter on the case-normalized word key (so `get_x`/`setX`/`SetX`/overload-style all pair; getter's spelling authoritative). A setter with no getter is a hard error (no write-only properties); a non-void (fluent) setter return is discarded by every rod and never gated. Rods: `def_property(_readonly)` / `def_prop_rw(_ro)` / `sol::property` / `addProperty` / luacats `---@field` (+`(read-only)` note). |
| `trust_bindable<T> = true` | Type-level form: trust `T` everywhere it appears. A specializable `bool` variable template, not an attribute. |
| `doc("text")` | Docstring for a class, namespace, function, function parameter, data member, or **enumerator**. Surfaced as `__doc__` by the Python rods (a data member's rides on its property, const → read-only; an **enumerator has no per-member slot the stubs surface, so its doc folds into the enum's *class* docstring as an *Attributes* section** — Google/NumPy/Sphinx per `DocStyle::format_enum`, carried into the `.pyi`; the carriage's `collect_enum_docs` gathers the participation-filtered pairs); ignored on namespace variables. Lua has no runtime docstring slot, so the sol2 rod ignores it at runtime — its home there is the generated **LuaCATS (`---@meta`) stub** (`welder::rods::luacats::rod`); an enumerator's doc there is a `---` comment above its entry in the `---@enum` table (LuaLS attaches it to the member). |
| `returns("text")` | Documents a function's return value (a `Returns:` block). Distinct from the summary `doc`. |
| `tparam("T", "text")` | Documents a template parameter (repeatable, ordered). Rides on the template itself; becomes `@tparam` in C++ docs, read back via `tparam_docs<Ent>()`. |
| `weld_as([lang…,] "name")` | Force this entity's target-language name **verbatim** (bypasses the name style). The name is last; zero or more `lang` markers precede it — none = all languages, one or several scope it. Repeat the annotation for a different name per language. |
| `return_policy([lang…,] rv::kind)` | How a callable's returned object is owned/converted — welder's backend-neutral spelling of the return-value policy. Kind (`welder::rv::` — `automatic`, `automatic_reference`, `take_ownership`, `copy`, `move`, `reference`, `reference_internal`, `none`) is last; optional leading `lang` markers scope it (none = all). Honored by the Python rods (pybind11 `return_value_policy` / nanobind `rv_policy`); the Lua rods ignore it (ownership is structural — value → VM-owned copy/move, pointer/reference → non-owning view), exactly as they ignore `doc`. A reference-category kind on a by-value return is a hard error in **every** language (dangling reference). `none` is nanobind-only (diagnosed on pybind11). |
| `keep_alive(nurse, patient)` | Tie one call entity's lifetime to another's (indices: `0` = return, `1` = first arg / a method's `this`, …): the `patient` is kept alive until the `nurse` is collected. Maps to pybind11/nanobind `keep_alive<Nurse, Patient>`; repeatable. A Python-binding concept — the Lua rods have no equivalent and ignore it. |

`policy::auto` from the original sketch is spelled `policy::automatic` (`auto` is
reserved). Resolution per language `L` (`reflect.hpp` `member_bound`): excluded for
`L` → false; else an `only` mark → true iff it names `L` (either policy); else
`automatic` → true; else (`opt_in`) → true iff explicitly included for `L` (a
`getter`/`setter` mark covering `L` also counts as the include).
**Class-NESTED types (member classes/enums) resolve like any other member** —
the outer's policy + the nested type's own marks, never a `weld` of their own —
and register under the outer's binding (`module.Outer.Inner` on the Python rods
and sol2; LuaBridge3 raw-moves the class table onto the outer; luacats emits the
dotted name), recursively; the bindability gate's registration oracle mirrors
the sweep exactly (a signature naming a non-participating nested type is a hard
error). `mark::exclude` + an own `weld` on a nested type = the manual
flat-registration escape. **Member type ALIASES** participate iff the target
FAILS the bindability gate (registering exactly what couldn't otherwise cross;
castable/welded/registered targets skipped — so `value_type` conventions cost
nothing and tack welds never sweep aliases); the class's own members gate
through a scope-aware oracle that sees its alias registrations; exclude+alias =
the class-scope rename escape. Details: `.claude/context/binding-features.md`
"Nested types" / "Member type aliases". **UNIONS never bind** (reading an
inactive member is UB; no sweep registers one): the gate, `weld_type` and the
namespace walk all hard-error with designed messages naming `std::variant` as
the fix; anonymous-union members and unnamed bit-fields are skipped
structurally (no name, no declarator to mark) — see `binding-features.md`
"Unions" + `bindability-gate.md`.
**Access admission precedes it** (bind_traits `member_access_admitted`): public
always; protected iff the resolution's optional
`protected_participates(mem, L, bound_into)` hook says so (default = the declaring
class's `weld_protected` annotation); private never, under any resolution. Every
per-member resolution hook takes a trailing `bound_into` reflection — the entity
whose binding receives the member (for class members the welded type, held fixed
through base flattening, ≠ `parent_of(mem)` for a flattened base's member). Marks
resolve **per overload, constructors included** (the carriage computes each name's
participating overload group from the resolution and hands it to the rod whole).
Constructors resolve symmetrically (opt_in binds only marked-include ctors), with
two fail-safes: the default ctor is exempt from opt_in's default-out (an implicit
one has nothing to mark; explicit marks on a declared one ARE honored), and
filtering that leaves a type with NO constructor is a hard error unless explicit
(mark::exclude on every ctor = a deliberate factory-only surface). The **copy**
ctor never binds as an init overload — it follows the default ctor's admission
pattern (implicit → rides along when copy-constructible; declared → explicit
marks honored) and reaches the rod as `add_constructors`' `Copyable` flag: the
Python rods bind it as the **subclass-faithful** copy protocol
`__copy__`/`__deepcopy__` *only* — never an unpythonic `T(other)` init overload
(a Python subclass copies as itself — type, `__dict__` and virtual dispatch
intact; `_copy_instance` copy-constructs the C++ payload — the trampoline for a
Python-derived shell, so virtuals keep dispatching — directly into the fresh
`__new__` shell, needing no visible constructor; `WELDER_PY_TRAMPOLINE(Tramp,
Base)` declares the copy-from-base trampoline ctor this needs), the Lua rods
ignore it (no copy protocol). **Move** ctors never bind — an `include`/`only` mark on one is a
designed hard error (`diag::marked_move_constructor`); `exclude` is a no-op. A class-template
**instantiation** is welded through a **namespace-scope alias** (`using IntBox =
Box<int>;`): the sweep enumerates the alias (never the specialization), which
supplies both the C++ spelling (for text-emitting rods) and the target name; only
`weld`/`weld_as` may sit on the alias (taking precedence over the template's — the
alias `weld` is the third-party-template opt-in); duplicate aliases of one
specialization and aliases to welded non-template types are hard errors. A welded
alias whose target is a **reference-semantic STL container** (`std::vector`,
`std::map`, `std::unordered_map` — `src/welder/containers.hpp`) is instead routed to
the Python rods' `bind_container` hook (`py::bind_vector`/`bind_map`,
`nb::bind_vector`/`bind_map`), binding the container **opaque / by reference** rather
than the default `<pybind11/stl.h>` copy: `obj.v.append(x)` writes through, and a
`std::vector` of scalars or **POD structs** exposes `data()` zero-copy to numpy — a
scalar vector via the buffer protocol (pybind11) / `nb::ndarray` (nanobind), a
POD-struct vector via a reflected, numpy-free `__array_interface__` (a STRUCTURED
array; `src/welder/rods/python/array_interface.hpp`). Requires `WELDER_OPAQUE(T)` (each Python rod's
`PYBIND11_MAKE_OPAQUE`/`NB_MAKE_OPAQUE`) at namespace scope; Python-only (a container
alias for a Lua rod is a designed `static_assert` — the Lua runtimes are reference-
semantic structurally). The `WELDER_OPAQUE` + alias boilerplate can be **auto-emitted**
by the build-time `welder::rods::opaque_containers::rod` (above). Details:
`.claude/context/binding-features.md` "Opaque, reference-semantic containers" +
`docs/content/guide/containers.md`. A `lang` is a bit in an `unsigned` mask; mask `0` on an
exclude/include spec is the sentinel for "all languages". The lang value space
is **open**: bits 0–15 are welder's, `welder::user_lang<Slot>` (lang.hpp) mints
user languages from 16–31 for out-of-tree rods (locked by
`tests/core/user_lang.cpp`; third-party rods should take their lang as a
template parameter so apps can re-point colliding slots).

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
