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
  - **enums** → `py::enum_`. A welded enum (scoped or unscoped) binds via `bind<E>`
    (dispatched from the public `bind<T>` by `is_enum_v`) or as a namespace/module
    member; the driver is `backend.hpp` `bind_enum`, the backend hooks `make_enum` /
    `add_enumerator` / `finish_enum`. Each **enumerator resolves like a data member**
    — the enum's `policy` (default automatic) plus per-enumerator `exclude`/`include`
    marks decide what binds (via the same `member_bound`); NB the C++ grammar puts an
    enumerator's annotation *after* its name (`South [[=welder::mark::exclude]]`).
    Excluding an enumerator does not renumber the rest. An **unscoped** enum also
    `export_values()` (enumerators visible unqualified on the enclosing module,
    mirroring C++); a **scoped** enum stays `E.Value`. The enum `doc` becomes the
    Python docstring; per-enumerator docs aren't supported (pybind11 `.value()` takes
    none). An enum-typed member/parameter binds because the enum is welded (bind the
    enum first, like a welded base). Tested: `tests/pybind11/cpp/enums.hpp` +
    `test_enums.py`.
  - **bindability gate ("pybind11-convertible").** Every surface welder is about to
    bind (data member, parameter, return type, namespace variable) must be a type
    pybind11 can convert to a *meaningful* Python value; otherwise it is a **hard
    compile error** (`static_assert` in `assert_bindable`, naming the offending
    type) — never a silent skip, since binding such a type yields a dead attribute
    *and* a stub referencing an unimportable type (breaking pybind11-stubgen). The
    fix in the message: weld the type, give it a pybind11 `type_caster`, or
    `mark::exclude` the member. The engine is **backend-agnostic** and lives in the
    core (`bindable.hpp`): a reflection-driven `stl_wrappers` table (`{^^std::vector,
    1}`, `{^^std::map, 2}`, `0` = all args for `tuple`/`variant`, …) matched via
    `template_of`, which recurses the value arguments of the STL containers,
    `optional`, `pair`/`tuple`/`variant` and the smart-pointer holders (so
    `std::vector<Unwelded>` is caught, not just a bare `Unwelded`). Reflection can
    enumerate a specialization's arguments but not tell which are value-bearing vs.
    infrastructure (allocator/comparator/hasher/deleter/extent), so the per-wrapper
    leading-arg *count* is the one thing the table still records. `bindable<B, T, L>()`
    folds these: a wrapper binds iff its value args do; a native/user-caster type
    binds as-is; otherwise it's a registration-needing class/enum, bound iff
    `welded_for`. The one **backend-specific** leaf — native-vs-needs-registration —
    is the backend's `has_native_caster<T>` (`caster_oracle`); pybind11 implements it
    as `!needs_registration<T>`, i.e. is T's caster the generic `type_caster_base`
    fallback (needs a `class_`/`enum_`) vs. native/self-contained? It is
    *conservative*: a compile-time read of T's caster type, so it reports whether T
    *needs* registration, never whether one will exist — a hand-registered
    (`py::class_`) but non-welded type still reads needs-registration and is rejected
    (the `trust_bindable` escape hatch below is the fix). Only a *self-contained*
    user `type_caster` (not derived from `type_caster_base`, e.g. via
    `PYBIND11_TYPE_CASTER`) flips native; a `type_caster_base`-derived caster still
    needs its class registered. "Native" is also relative to the TU's includes —
    `std::complex`/`function`/`chrono`/`filesystem::path` are native only with their
    converter header (`<pybind11/complex.h>`, …). *Not* exhaustive for a non-STL
    wrapper with its own caster — its elements aren't recursed (an opaque bindable
    leaf).
    Negative-compile cases live in `tests/pybind11/cpp/neg/` (`negcompile.*` CTests,
    `WILL_FAIL`). Two **escape hatches** cover a type welder can't see is
    registered (hand-written pybind11 `class_`, a third-party library's bindings) —
    both backend-agnostic, in the core vocabulary, and suppressing the gate so the
    user then owns the registration: a member mark `[[=welder::mark::trust_bindable]]`
    (trusts that member's type / a callable's whole signature; `reflect.hpp`
    `trusted_for`, honored via `bindable.hpp` `assert_member_bindable` /
    `assert_callable_bindable`), and a type-level `welder::trust_bindable<T> = true`
    (trusts T everywhere, folded into `bindable()` so it also clears `T*` / `const
    T&` / `std::vector<T>`). Tested in `tests/pybind11/cpp/trust.hpp` +
    `test_trust.py`. A richer future point could map T to a stub type name
    (`bindable_as<T>`); still TODO. **A self-contained pybind11 `type_caster<T>`**
    (not deriving from `type_caster_base`, e.g. via `PYBIND11_TYPE_CASTER`) needs
    *neither* weld nor trust: it displaces the fallback, so `needs_registration` is
    false and the gate passes automatically, and the caster's `const_name` stubs the
    member/parameter cleanly (e.g. as `float`). The one requirement (standard
    pybind11, not welder-specific): the caster must be visible **before** welder
    binds any type using T — gcc-16 defers the point of instantiation to end-of-TU
    so a later caster in the *same* TU also works, but that is ill-formed-NDR to
    rely on; keep the caster ahead of the bind. Tested in
    `tests/pybind11/cpp/caster.hpp` + `test_caster.py`.
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
    Variable docs are intentionally ignored by binding backends (no attribute
    `__doc__` in Python); the Doxygen filter surfaces them on the C++ side. Doc
    text is stored *inline*
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
  - **C++ docs via a Doxygen INPUT_FILTER** (`tools/welder_doxygen_filter.py`) —
    the C++ API documents itself from the *real sources*. Background: Doxygen's
    native parser copes with the C++26 sources but silently discards `[[=…]]`
    annotations, and it has **no plugin system** — its extension point is
    `INPUT_FILTER`, a per-file program whose stdout is what Doxygen parses
    (disk untouched). The filter translates the doc vocabulary into Doxygen
    comments: `doc` → `/** … */`, `returns` → `@return`, `tparam` → `@tparam`;
    `weld`/`policy`/`mark`/`trust_bindable` are stripped (doc *scope* control
    is Doxygen-native — `EXCLUDE_SYMBOLS`). The parsing lives in a **Lark
    grammar** (`tools/welder_doxygen_filter.lark`, needs `pip install lark`),
    two layers sharing one lexer: layer 1 lexes the *C++ lexical soup* —
    comments, string/char literals, raw strings (delimiter by backreference)
    are single **atomic tokens**, so annotation-shaped text in a string or a
    commented-out line is invisible downstream; lexing is **total** (one-char
    `PUNCT` catch-all: any bytes lex); `<<`/`>>`/`<=`/`>=`/`<=>`/`->` are
    **maximal-munch `OP` tokens** so shifts/comparisons/spaceship/arrow can
    never be mistaken for template angles (and a `>>` closes *two* angles,
    the C++11 rule); backslash-newline **line splices** are honored in line
    comments and string/char literals (and correctly *not* in raw strings);
    layer 2 parses one block's
    content (`start='attr_list'`): top-level comma split with nested balanced
    groups, elements classified in the driver — welder docs translated
    (adjacent string literals concatenate, phase-6 style; `=::welder::…`
    recognized), other
    welder specs stripped, non-welder elements (`[[nodiscard]]`,
    `[[deprecated("…")]]`, foreign annotations) re-emitted in place. Block
    *extents* stay a small token scan in the driver — deliberately not
    grammar work, since `]]` is context-dependent in C++ (`a[b[0]]`); an
    unterminated `[[` yields no block (editing around it could swallow code).
    **Fail-safety contract** (locked by the `hostile.hpp` golden): lexing is
    total; each block transforms in its own try/except — an unparseable block
    stays verbatim (stderr note); a last-resort try/except (missing `lark`
    included) emits the whole file unchanged; non-UTF-8 survives via
    surrogateescape + byte-exact stdout; exit 0 in all these cases — a doc
    build can never crash on someone's code, worst case it loses welder
    annotations for that file. Placement stays positional in the driver
    (probed): keyword-position annotations hoist before
    `struct`/`class`/`union`/`enum [class]`/`namespace`, before
    `template <…>` head(s), *and* before a **requires-clause** between head
    and keyword (all constraint endings: `)`, a concept-id's `>`/`>>`,
    `&&`-chains, `requires requires`; conservative bail-out); parameter docs
    become trailing `/**< */` before
    the parameter's top-level `,`/`)` — the `<` **template-angle vs
    less-than ambiguity** is resolved by *tentative matching* (`angle_probe`:
    a `<` after an identifier counts as angles only when a matching `>`
    plausibly closes it — rejects on `;`, top-level `=`, or the enclosing
    construct ending first), so `std::map<K, V> x = {{1, "one"}}` *and*
    default arguments like `flags = 1 << 4`, `wide = sizeof(int) < 8` all
    survive; enumerator/member docs become trailing `/**<`, placed **after
    the initializer** when one follows (`Low [[…]] = 1` → `Low = 1 /**< */`;
    before the `=` Doxygen mis-parses it for members); anything else becomes
    a preceding `/** */` block, indent
    preserved. **Templates document naturally** — the filter is textual, so
    annotations inside templates translate like anywhere else (reflection
    cannot read an uninstantiated template; the filter doesn't care). That is
    the dedupe story: one annotation on/in a template feeds the C++ reference
    *and* — via instantiation reflection — the bound instantiation's runtime
    docstring. Known limits (documented conventions): annotations must be
    spelled `welder::`-qualified (`::welder::` works, a namespace *alias*
    does not); the filter is preprocessor-blind (annotations in macro bodies
    transform textually; a parameter list split across `#if` branches can
    misplace a param doc). One *Doxygen*-side limit (probed 1.16/1.17, not a
    filter defect — the golden proves our output right): a **bare
    unparenthesized `<` comparison in a default argument** derails Doxygen's
    own parameter parsing, losing that parameter's doc and the rest of that
    list — write `(sizeof(T) < 8)`, as the corpus `clash()` does; the corpus
    `bare()` case tracks the loss (e2e `DOXYGEN_LOSES`, flips visibly if a
    future Doxygen fixes it). NB Doxygen auto-links doc words matching
    entity names (`<ref>`) and entity-escapes `&<>`, so the e2e greps a
    tag-stripped, entity-decoded view of the XML. Usage: `INPUT_FILTER =
    "python3 …/welder_doxygen_filter.py"` (or `FILTER_PATTERNS` per
    extension). Tested in `tests/doxyfilter/` (run with the uv venv Python,
    which pins `lark`): two byte-exact goldens — `doxyfilter.golden.corpus`
    (placement) and `doxyfilter.golden.hostile` (fail-safety; that corpus
    contains raw non-UTF-8, so it's written/regenerated programmatically) —
    plus an attachment e2e asserting every doc text lands in Doxygen XML
    (`doxyfilter.doxygen`, self-skips without doxygen). The `doxyfilter-html`
    target (in ALL when doxygen is present) renders the filtered corpus to
    `build/…/tests/doxyfilter/html/index.html` for eyeballing.
  - **the documentation site** (`docs/`, gated by `WELDER_BUILD_DOCS`, default
    **OFF**) — one modern site, two toolchains cleanly separated, wired by
    `docs/CMakeLists.txt`:
    - **mkdocs-material** renders the hand-written narrative *guide* (public API
      with runnable examples, architecture) from `docs/content/*.md` (branded to a
      deep-orange/amber "spark" palette with a light/dark toggle that follows the
      OS; `content/stylesheets/extra.css` for the few tweaks Material doesn't own).
      `docs/mkdocs.yml` is the config (`docs_dir: content`; superfences+mermaid,
      tabbed, admonitions, code copy/annotate).
    - **Doxygen** renders the *full C++ reference* — public API **and** `detail/`
      internals **and** every template/concept (`EXTRACT_ALL`/`EXTRACT_PRIVATE`/
      `INTERNAL_DOCS`, source browser on) — from the real `src/welder/**.hpp`
      through the **same INPUT_FILTER** as above, so `[[=welder::doc/returns/
      tparam]]` come through. `docs/Doxyfile.in` (configured), landing on
      `docs/api_mainpage.md` (`USE_MDFILE_AS_MAINPAGE`). Themed with
      **doxygen-awesome-css** (v2.3.4, git-cloned into the build dir at configure
      time — degrades gracefully to the stock theme on network/`git` failure),
      sidebar-only layout + a dark-mode toggle wired via a build-time-generated,
      patched header (`docs/patch_doxygen_header.py`; failure → stock header, still
      the base awesome theme); `docs/doxygen-extra.css` retunes its accent to the
      same spark palette. Every `src/welder/**.hpp` now carries `/** */` Doxygen
      blocks (first-sentence autobrief, `@param`/`@tparam`/`@return`, trailing
      `/**< */` on members/enumerators; function-body comments stay `//`), so the
      reference renders full prose for the public API *and* internals — a
      warning-free Doxygen run (verified file-by-file). The house doc-comment style
      is `/** */` not `///` (less noise) — see [[welder-doc-comment-style]].
    - Both run from an **isolated uv env** (`docs/pyproject.toml` → mkdocs-material
      + lark; `docs/uv.lock` committed like `tests/`), the same interpreter used
      for the Doxygen filter (the one guaranteed to have `lark`). Doxygen writes the
      reference into `docs/content/api/` (**inside** mkdocs' `docs_dir`, generated +
      gitignored), so mkdocs copies it into the site as static files for *both*
      `build` and `serve` — hence Doxygen runs **before** mkdocs. The guide's
      `reference.md` links to `../api/index.html` (a raw `<a>` so mkdocs doesn't
      warn).
    - **Targets:** `welder-docs` (Doxygen → `content/api`, *then* `mkdocs build`
      copies it alongside the guide → `<build>/docs/site/index.html`; order matters,
      so both are steps of one target); `welder-docs-serve` (Doxygen once, then
      `mkdocs serve` — the guide live-reloads, the reference is a static snapshot
      that *is* served, no 404). Both self-skip (with a warning) if `doxygen`/`uv`
      are absent. Graphviz (`dot`) is optional (class graphs). NB mkdocs mermaid
      diagrams: don't hardcode node `fill`/`color` (Material flips the label color
      per theme and htmlLabels ignore per-node `color`, giving unreadable text in
      dark mode) — use `stroke` accents and let Material's amber-accent default fill
      stand.
  - **template ↔ annotation semantics** (locked in by
    `tests/core/template_annotations.cpp`, compile-only static_asserts):
    annotations on a template *declaration* are readable through every
    **instantiation** — with primary / partial / explicit-specialization
    precedence, and including member, parameter and `weld`/mark annotations;
    `substitute()`d function/variable-template instantiations carry them too.
    Only the *uninstantiated* template (or concept) reflection refuses
    `annotations_of` (P2996 restriction) — but any instantiation handed to
    welder has full docs, and
    `weld` on a class template makes `bind<Welded<int>>(m, "name")` legitimate
    today — the explicit name is required (a specialization `has_identifier` ==
    false; the `identifier_of` name default would throw).

Properties, and additional languages (Lua, …) are designed-for but **not yet
implemented**. (Enums and custom type converters now are — see above.)

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
| `mark::trust_bindable` / `mark::trust_bindable(lang...)` | Vouch that this member's type (or a callable's whole signature) is representable outside welder's view (e.g. hand-registered with pybind11); suppresses the bindability gate. |
| `trust_bindable<T> = true` | Type-level form of the above: trust `T` everywhere it appears (member, param, return, container element). A specializable `bool` variable template, not an attribute. |
| `doc("text")` | Docstring for a class, namespace, function, or function parameter. Surfaced as the target language's `__doc__`; ignored on variables. |
| `returns("text")` | Documents a function's return value (a `Returns:` block in its docstring). Distinct from the function's summary `doc`. |
| `tparam("T", "text")` | Documents a template parameter (repeatable, ordered). Rides on the *template itself* — template parameters aren't reflectable entities. Becomes `@tparam` in the C++ docs (Doxygen filter); read back via `tparam_docs<Ent>()` off an instantiation for backend docstrings. |

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

Language-agnostic **core** + pluggable **backends**, joined by **static
polymorphism**. The core owns *all* the reflection work — deciding **what** binds
(`bind_traits.hpp`), whether each type is representable (`bindable.hpp`), and
walking types/namespaces/bases to drive a binding (`backend.hpp`'s generic
driver). A **backend** is a stateless policy struct satisfying the `welder::backend`
concept: it supplies only the *emission primitives* (how to register a class /
method / property / module attribute in its framework) and never re-implements
the traversal or annotation semantics. Adding a language = writing one backend
struct + thin public wrappers; the core is reused verbatim. The core never depends
on a backend.

`src/` is the include root, so every public include starts from `welder/`.

```
src/welder/
  detail/config.hpp     WELDER_EXPORT macro (export under the module, else empty)
  lang.hpp              enum class lang                       — std-free vocabulary
  annotations.hpp       weld / policy / mark / doc + mask helpers — std-free vocabulary
  reflect.hpp           welded_for / policy_of / member_bound / trusted_for / public_bases — uses <meta>
  doc.hpp               doc_of / return_doc_of / param_docs / doc styles / function_docstring — uses <meta>
  bind_traits.hpp       backend-agnostic "what binds": param/ctor/method/operator/namespace-member selectors + native-base collection — uses <meta>
  bindable.hpp          caster_oracle concept + generic bindability gate (STL-wrapper recursion) — uses <meta>
  backend.hpp           the `welder::backend` concept (emission contract) + generic driver (bind_type / bind_namespace_driver / build_module_driver)
  module.hpp            WELDER_MODULE(ns, backend) entry-point dispatch macro
  welder.hpp            header-only umbrella: lang+annotations+reflect+doc
  welder.cppm           the single `export module welder;` (exports vocabulary only)
  backends/
    pybind11.hpp        pybind11 backend: struct detail::backend (emission primitives) + public bind<T> / bind_namespace / build_module wrappers over the driver
    CMakeLists.txt      target: welder::pybind11  (nanobind / lua planned here)
src/CMakeLists.txt      targets: welder::headers / welder::module
cmake/
  WelderPybind11Stubgen.cmake  welder_pybind11_generate_stubs() — .pyi via pybind11-stubgen
tools/
  welder_doxygen_filter.py     Doxygen INPUT_FILTER driver: welder annotations → Doxygen comments (needs `lark`)
  welder_doxygen_filter.lark   its grammar: C++ lexical soup (layer 1) + attribute-list (layer 2)
examples/
  python_poc/             consumes `import welder;`
  python_poc_headeronly/  consumes welder header-only
  welder_module/          whole-module binding via WELDER_MODULE
docs/                     the documentation site (gated by WELDER_BUILD_DOCS, OFF)
  CMakeLists.txt          targets welder-docs / welder-docs-serve; provisions the uv env, fetches doxygen-awesome, patches the Doxygen header
  mkdocs.yml              mkdocs-material config (docs_dir: content)
  content/                the narrative guide (index, guide/*, architecture, reference) + stylesheets/extra.css
  Doxyfile.in             configured → Doxygen C++ reference (src/welder/** via the INPUT_FILTER) into content/api/ (gitignored; mkdocs copies to site/api)
  api_mainpage.md         the Doxygen landing page (USE_MDFILE_AS_MAINPAGE)
  doxygen-extra.css       spark-palette retune over doxygen-awesome-css
  patch_doxygen_header.py injects the doxygen-awesome dark-mode/extension JS into the generated header
  pyproject.toml/uv.lock  isolated docs env: mkdocs-material + lark
```

`bind_traits.hpp`, `bindable.hpp` and `backend.hpp` are part of the reflection
layer (like `reflect.hpp`/`doc.hpp`): header-only, `<meta>`-using, **not** part of
the `welder` module, and they do **not** include `annotations.hpp` (the vocabulary
arrives first via `import welder;` or `welder.hpp`). `doc.hpp` follows the same
rule. `module.hpp` is macro-only and backend-agnostic; each backend header defines
its `WELDER_DETAIL_MODULE_ENTRY_<backend>` expansion.

### The backend interface (static polymorphism)

A backend is a stateless struct `B` satisfying `welder::backend` (`backend.hpp`).
The core's generic driver (`welder::detail::bind_type` / `bind_namespace_driver` /
`build_module_driver`) is templated on `B` and calls its members; the public
`welder::pybind11::bind` / `bind_namespace` / `build_module` are one-line wrappers
that plug in `pybind11::detail::backend`. `B` provides:

- **Associated:** `static constexpr lang language;` the target language;
  `using module_type = …;` the module handle; `template<class T> static constexpr
  bool has_native_caster;` — the `caster_oracle` leaf: is `T` convertible *without*
  welder registering a class for it? (false ⇒ welder requires `T` welded). This is
  the one bindability fact the core can't know; the STL-wrapper recursion in
  `bindable.hpp` is shared.
- **Type binding:** `make_class<T, Bases…>`, `add_default_ctor`, `add_constructor<Ctor>`,
  `add_aggregate_constructor<T>`, `add_field<Mem>`, `add_method<Fn>`,
  `add_static_method<Fn>`, `add_operator<Fn>`, and `consteval special_method_name(op)`
  (the operator→target-name map, e.g. pybind's `operator+`→`__add__`; nullptr =
  not exposed, which also gates operator eligibility in the driver).
- **Enum binding:** `make_enum<E>`, `add_enumerator<Enum>`, `finish_enum<E>` (the
  whole-enum finalizer, e.g. pybind's `export_values()` for unscoped enums).
- **Namespace/module binding:** `open_module`→ a per-(sub)module *session* (backend
  scratch state — pybind uses it to batch live variable properties), `set_module_doc`,
  `add_function<Fn>`, `add_variable<Var>` (const→snapshot, else a live property),
  `add_submodule`, `close_module` (finalize the session).

The concept statically checks the associated types and the module machinery; the
class/per-member hooks are templated on a reflection, so they are
contract-by-documentation, enforced when the driver instantiates. A nanobind
backend is nearly a copy of the pybind11 one (same class-handle model); a Lua
backend implements the same ~16 primitives against Lua's C API.

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

The documentation site (needs `doxygen` + `uv`; off by default):

```bash
cmake --preset welder-gcc16 -DWELDER_BUILD_DOCS=ON   # configure: syncs the uv docs env, fetches doxygen-awesome
cmake --build --preset welder-gcc16 --target welder-docs
# open build/welder-gcc16/docs/site/index.html   (guide);  .../site/api/index.html (C++ reference)
cmake --build --preset welder-gcc16 --target welder-docs-serve   # live-reload the guide while writing prose
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
