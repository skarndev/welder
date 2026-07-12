# Docstrings, the Doxygen filter & the docs site

Read when: working on docstrings (`doc.hpp`), the Doxygen INPUT_FILTER
(`tools/welder_doxygen_filter.*`), or the documentation site (`docs/`). User-facing
versions: `docs/content/guide/docstrings.md` and `cpp-docs.md`.

House doc-comment style is `/** */` not `///` (less noise) — rely on autobrief.

## Docstrings (`doc.hpp`, backend-agnostic)
`[[=welder::doc("…")]]` on a class/namespace/function/parameter,
`[[=welder::returns("…")]]` on a function. A return value isn't a reflectable
entity, so its doc rides on the function as a *distinct* spec type
(`return_doc_spec`), told apart from the summary by spec type.
`function_docstring<^^Fn, Style>()` folds summary + param docs + return doc (via a
`function_doc` parts struct, extensible to future `Raises:`/`Note:` without
re-breaking the style API) under a pluggable style; surfaced as Python `__doc__`.
`doc.hpp` keeps only the neutral `doc_style` concept + `function_docstring` (no
default style); the concrete styles live in `<welder/rods/python/doc_style.hpp>`
under `welder::rods::python`, shared by both Python rods. Three are shipped —
`google_style` (→ `Args:`/`Returns:`), `numpy_style` (underlined
`Parameters`/`Returns` numpydoc sections, bare `name` since welder has no type
text for the `name : type` colon) and `sphinx_style` (`:param`/`:returns:` reST
field lists) — sharing `detail::{append_indented,blank_line,any_param_doc}`. Each
`format()` is **`constexpr`** (unit-tested by `static_assert` in
`tests/core/doc_styles.cpp`, same shape as `doc_cleandoc.cpp`); that is *why* they
are hand-rolled, not written with `std::format` (which is not `constexpr` in
libstdc++ on gcc-16 — the compile-time doc paths `cleandoc`/`annotation_text_of`
rule it out anyway, so all string assembly stays `constexpr`-clean).

**Style selection is a rod template parameter.** The Python rods are
`welder::rods::{pybind11,nanobind}::rod<DocStyle = google_style>`; `rod<>` is the
default (Google) rod (the name most code uses), `rod<numpy_style>` /
`rod<sphinx_style>` pick the others. Only the rod's `_def_function` reads
`DocStyle` (→ `function_docstring<Fn, DocStyle>`); the driver is style-agnostic
(doc formatting is a Python-flavor concern — the Lua rods ignore it, luacats has
its own). Because `rod` became a template, every use spells `rod<>` (module macros,
tests, `naming.hpp`). E2e: the shared `tests/common/cpp/doc.hpp` re-welds
`documented::add` through `WELDER_TEST_{NUMPY,SPHINX}_WELDER` seams (defined per
backend beside `WELDER_TEST_WELDER`) into `documented_numpy`/`documented_sphinx`
submodules, asserted in `tests/python/test_doc.py` for both backends.

**Multiline docs work** — a `doc`/`returns`/param text is just a `const char[N]`,
so a raw string literal (`R"(…)"`) with newlines/blank lines/quotes/backslashes
flows through `fixed_string` to `__doc__` (common case: function docs carrying
`>>> `-style examples). Such text is **dedented** at read time by `doc.hpp`
`cleandoc` (Python `inspect.cleandoc`/PEP 257 semantics: strip the first line,
remove the indentation common to the rest, trim leading/trailing blank lines —
relative indentation of an example block is kept), applied centrally in
`annotation_text_of` so class/namespace/function/param/returns docs can all be
indented to match the source without that indentation leaking; tabs are not
expanded (indent with spaces). `google_style` then indents each param/returns
block's *continuation* lines so multiline entries stay readable (tested: `doc.hpp`
`Gadget`/`combine` + `test_doc.py`). NB the Doxygen filter path (C++ docs) is
textual and does not dedent — Doxygen does its own.

**Data-member docs** land on the member's Python attribute: pybind11 binds members
as *properties* (data descriptors), and `add_field` (`rods/python/pybind11/rod.hpp`) passes
`doc_of<Mem>()` as the property docstring — so it reaches `__doc__` and the `.pyi`
stubs. A **const** member is bound read-only (`def_readonly`; `def_readwrite`'s
setter would not compile), a mutable one read/write (`def_readwrite`); only the
getter's doc is surfaced (a Python `property` has one `__doc__`), so no setter
docstring is emitted. Tested via `Circle.r` / `Marker` in `doc.hpp` + `test_doc.py`.
**Namespace-variable** docs remain intentionally ignored by binding rods (a bound
module attribute has no `__doc__`); the Doxygen filter surfaces them on the C++ side.
Doc text is stored *inline* (`fixed_string`) — a `const char*` to a literal isn't a
permitted annotation constant on gcc-16.

**Lua (sol2):** Lua has no runtime docstring slot, so the sol2 rod ignores
`doc`/`returns` at runtime (the same `doc_of` extraction still runs; there is just
no sink). Their Lua home is a **generated LuaCATS (`---@meta`) definition file** —
the `.pyi` analogue — *reflection-emitted at build time*, not scraped from a loaded
module (a loaded sol2 usertype exposes nothing introspectable). **Implemented** as
the `welder::rods::luacats::rod` (`src/welder/rods/lua/luacats/rod.hpp`): a
text-emitting `welder::rod` that plugs the *same* generic driver as sol2 (so
member selection / base flattening / policy-marks / the bindability gate are reused
verbatim), swapping the emission primitives to append LuaCATS text — `--- ` summary
lines, `---@field`/`---@param`/`---@return name type description` tags, `---@class X
: Base`, `---@enum`, `---@operator`. The one thing sol2 didn't need is the
C++→LuaCATS type map (`lua_type_string`; see build-test-run.md). Overloaded
methods/constructors/free functions collapse to one documented `function` + idiomatic
`---@overload fun(…)` lines (grouped via the shared `*_overload_set` selectors in
`rods/lua/overloads.hpp` — `welder::rods::lua`, shared with the sol2 rod — since the
driver still visits overloads one at a time; the primary is the first overload with a
doc, so its `@param`/summary text survives); a const
member's read-only-ness is a `(read-only)` description note (LuaCATS has no read-only
field tag). Class/enum blocks flush by RAII (the driver has no "finish class" hook) —
constructors accumulate on the `class_writer` and render as one `.new` group at flush
— and module/submodule tables (`ns = {}`) are declared shallowest-first ahead of the
body. Build a stub with
`welder_luacats_generate_stub()` over a `WELDER_LUACATS_MAIN(<ns>)` generator; the
golden lives in `tests/luacats/`, and — when a `lua-language-server` is installed —
the `stubcheck.luacats` CTest lints the emitted stub (the Lua analogue of mypy over
the `.pyi`; comparison/subscript operators are dropped from the stub since LuaCATS
`---@operator` can't name them). See `binding-features.md` (Lua specifics) and
build-test-run.md (the stub build/test path).

## C++ docs via a Doxygen INPUT_FILTER (`tools/welder_doxygen_filter.py`)
The C++ API documents itself from the *real sources*. Doxygen's native parser
copes with the C++26 sources but silently discards `[[=…]]` annotations, and it has
**no plugin system** — its extension point is `INPUT_FILTER`, a per-file program
whose stdout is what Doxygen parses (disk untouched). The filter translates the doc
vocabulary into Doxygen comments: `doc` → `/** … */`, `returns` → `@return`,
`tparam` → `@tparam`; `weld`/`policy`/`mark`/`trust_bindable` are stripped (doc
*scope* control is Doxygen-native — `EXCLUDE_SYMBOLS`).

The parsing lives in a **Lark grammar** (`tools/welder_doxygen_filter.lark`, needs
`pip install lark`), two layers sharing one lexer:
- **Layer 1** lexes the *C++ lexical soup* — comments, string/char literals, raw
  strings (delimiter by backreference) are single **atomic tokens**, so
  annotation-shaped text in a string or a commented-out line is invisible
  downstream; lexing is **total** (one-char `PUNCT` catch-all: any bytes lex);
  `<<`/`>>`/`<=`/`>=`/`<=>`/`->` are **maximal-munch `OP` tokens** so
  shifts/comparisons/spaceship/arrow can never be mistaken for template angles (and
  a `>>` closes *two* angles, the C++11 rule); backslash-newline **line splices**
  are honored in line comments and string/char literals (and correctly *not* in raw
  strings).
- **Layer 2** parses one block's content (`start='attr_list'`): top-level comma
  split with nested balanced groups, elements classified in the driver — welder
  docs translated (adjacent string literals concatenate, phase-6 style;
  `=::welder::…` recognized), other welder specs stripped, non-welder elements
  (`[[nodiscard]]`, `[[deprecated("…")]]`, foreign annotations) re-emitted in place.

Block *extents* stay a small token scan in the driver — deliberately not grammar
work, since `]]` is context-dependent in C++ (`a[b[0]]`); an unterminated `[[`
yields no block (editing around it could swallow code).

**Fail-safety contract** (locked by the `hostile.hpp` golden): lexing is total;
each block transforms in its own try/except — an unparseable block stays verbatim
(stderr note); a last-resort try/except (missing `lark` included) emits the whole
file unchanged; non-UTF-8 survives via surrogateescape + byte-exact stdout; exit 0
in all these cases — a doc build can never crash on someone's code, worst case it
loses welder annotations for that file.

**Placement** stays positional in the driver (probed): keyword-position annotations
hoist before `struct`/`class`/`union`/`enum [class]`/`namespace`, before
`template <…>` head(s), *and* before a **requires-clause** between head and keyword
(all constraint endings: `)`, a concept-id's `>`/`>>`, `&&`-chains, `requires
requires`; conservative bail-out); parameter docs become trailing `/**< */` before
the parameter's top-level `,`/`)` — the `<` **template-angle vs less-than
ambiguity** is resolved by *tentative matching* (`angle_probe`: a `<` after an
identifier counts as angles only when a matching `>` plausibly closes it — rejects
on `;`, top-level `=`, or the enclosing construct ending first), so `std::map<K, V>
x = {{1, "one"}}` *and* default arguments like `flags = 1 << 4`, `wide =
sizeof(int) < 8` all survive; enumerator/member docs become trailing `/**<`, placed
**after the initializer** when one follows (`Low [[…]] = 1` → `Low = 1 /**< */`;
before the `=` Doxygen mis-parses it for members); anything else becomes a
preceding `/** */` block, indent preserved.

**Templates document naturally** — the filter is textual, so annotations inside
templates translate like anywhere else (reflection cannot read an uninstantiated
template; the filter doesn't care). That is the dedupe story: one annotation on/in
a template feeds the C++ reference *and* — via instantiation reflection — the bound
instantiation's runtime docstring.

**Known limits** (documented conventions): annotations must be spelled
`welder::`-qualified (`::welder::` works, a namespace *alias* does not); the filter
is preprocessor-blind (annotations in macro bodies transform textually; a parameter
list split across `#if` branches can misplace a param doc). One *Doxygen*-side
limit (probed 1.16/1.17, not a filter defect — the golden proves our output right):
a **bare unparenthesized `<` comparison in a default argument** derails Doxygen's
own parameter parsing, losing that parameter's doc and the rest of that list —
write `(sizeof(T) < 8)`, as the corpus `clash()` does; the corpus `bare()` case
tracks the loss (e2e `DOXYGEN_LOSES`, flips visibly if a future Doxygen fixes it).
NB Doxygen auto-links doc words matching entity names (`<ref>`) and entity-escapes
`&<>`, so the e2e greps a tag-stripped, entity-decoded view of the XML.

Usage: `INPUT_FILTER = "python3 …/welder_doxygen_filter.py"` (or `FILTER_PATTERNS`
per extension). Tested in `tests/doxyfilter/` (run with the uv venv Python, which
pins `lark`): two byte-exact goldens — `doxyfilter.golden.corpus` (placement) and
`doxyfilter.golden.hostile` (fail-safety; that corpus contains raw non-UTF-8, so
it's written/regenerated programmatically) — plus an attachment e2e asserting every
doc text lands in Doxygen XML (`doxyfilter.doxygen`, self-skips without doxygen).
The `doxyfilter-html` target (in ALL when doxygen is present) renders the filtered
corpus to `build/…/tests/doxyfilter/html/index.html` for eyeballing.

## The documentation site (`docs/`, gated by `WELDER_BUILD_DOCS`, default OFF)
One modern site, two toolchains cleanly separated, wired by `docs/CMakeLists.txt`:

- **mkdocs-material** renders the hand-written narrative *guide* (public API with
  runnable examples, architecture) from `docs/content/*.md` (branded to a
  deep-orange/amber "spark" palette with a light/dark toggle that follows the OS;
  `content/stylesheets/extra.css` for the few tweaks Material doesn't own).
  `docs/mkdocs.yml` is the config (`docs_dir: content`; superfences+mermaid, tabbed,
  admonitions, code copy/annotate).
- **Doxygen** renders the *full C++ reference* — public API **and** `detail/`
  internals **and** every template/concept (`EXTRACT_ALL`/`EXTRACT_PRIVATE`/
  `INTERNAL_DOCS`, source browser on) — from the real `src/welder/**.hpp` through
  the **same INPUT_FILTER** as above, so `[[=welder::doc/returns/tparam]]` come
  through. `docs/Doxyfile.in` (configured), landing on `docs/api_mainpage.md`
  (`USE_MDFILE_AS_MAINPAGE`). Themed with **doxygen-awesome-css** (**v2.4.2**,
  git-cloned into a version-named build dir at configure time — degrades gracefully
  to the stock theme on network/`git` failure).
  **Version matters:** Doxygen 1.11+ rewrote its navigation to a `#page-nav-*`
  sidebar; doxygen-awesome < 2.4 styles only the legacy `#nav-tree`, leaving
  Doxygen 1.17's real sidebar/tree/resize-handle/search **unstyled and white in
  dark mode** — v2.4.x is required. We use the **base theme only** (no
  `sidebar-only` add-on: page-nav is already a sidebar, and sidebar-only targets
  the legacy tree + was cropping the search box).
  **Doxygen 1.17 ships no jQuery**, so *all* of doxygen-awesome's `init()`-based
  extensions (dark-mode toggle, fragment-copy, paragraph-link, tabs) silently fail
  — they call `$(function(){…})`. So `docs/patch_doxygen_header.py` (build-time;
  failure → stock header, still the base theme) injects **our own** controls in
  plain, self-contained JS — **no doxygen-awesome JS is loaded at all**. The theme
  keys dark mode purely on `html.dark-mode` (+ `prefers-color-scheme`), so we manage
  that class ourselves: apply an explicit `dark-mode`/`light-mode` on load (saved
  key `welder-color-scheme`, else the OS pref — *always* explicit, so our accent
  overrides win over awesome's higher-specificity `@media(prefers-color-scheme:dark)
  html:not(.light-mode)` dark accent, which `docs/doxygen-extra.css` therefore also
  overrides in that media form), a `<button>` toggle placed inline next to the
  search box with a **delegated** document-level click (a direct listener didn't
  fire reliably in the search container), and the **project title linked back to
  the guide** (`$relpath^../index.html`). The welder **mark** — the "W" weld path
  with a struck spark, single deep-orange so it reads on both themes
  (`docs/welder-logo.svg`) — is set via `PROJECT_LOGO`. The mkdocs header uses the
  single-colour *light* variant (`docs/content/assets/welder-mono.svg`, set as
  `theme.logo`) since that bar is amber; the two-tone README lockups
  (`welder-lockup-{light,dark}.svg`, swapped via `<picture>`) live alongside it.
  `HTML_COLORSTYLE` stays `LIGHT` with **default HUE/SAT** (custom values bake warm,
  non-theme-adapting colors into doxygen's own CSS). The interactive-toc extension
  is left **off** (it makes a full-height right panel that fights the layout).
  `docs/doxygen-extra.css` only retunes the accent to the spark palette, styles
  `::selection` (unset by the theme → else unreadable in dark mode), and tames
  doxygen's literal-cyan jump-to-anchor `.glow` — the theme handles
  nav/inline-code/TOC/member boxes. Every `src/welder/**.hpp` carries `/** */`
  Doxygen blocks (first-sentence autobrief, `@param`/`@tparam`/`@return`, trailing
  `/**< */` on members/enumerators; function-body comments stay `//`) — a
  warning-free Doxygen run (verified file-by-file).
- Both run from an **isolated uv env** (`docs/pyproject.toml` → mkdocs-material +
  lark; `docs/uv.lock` committed like `tests/`), the same interpreter used for the
  Doxygen filter (the one guaranteed to have `lark`). Doxygen writes the reference
  **out of source** (`<bin>/docs/reference/api`); the `inject_reference.py` mkdocs
  hook grafts it into `<site>/api` after every build/serve rebuild (path via the
  `WELDER_DOXYGEN_API` env var set by the CMake targets; unset → silent no-op), so
  `docs_dir` stays the clean worktree. The guide's `reference.md` links to
  `../api/index.html` (a raw `<a>` so mkdocs doesn't warn).
- **API auto-links (`docs/apilink.py`):** Doxygen also emits a **tag file**
  (`GENERATE_TAGFILE` → `<bin>/docs/reference/welder.tag`, *next to* `api/` so it
  is never published). The `apilink.py` mkdocs hook loads it (path derived from
  `WELDER_DOXYGEN_API`; missing → no-op) and wraps inline `<code>` spans naming
  welder API entities in links to their reference pages. Resolution order: exact
  qualified name → implicit `welder::` prefix → `welder::welder::` member (the
  entry points) → unique short name (len ≥ 4, and never the `_DENY` words —
  `detail`, `impl`, pybind11's `export_values`, … — which read as the *user's*
  code when bare). Normalization strips `[[=…]]` wrappers, template args and call
  parens (`weld_type<T>(m)` → `weld_type`); existing `<a>`/`<pre>` regions are
  left untouched. Renamed/removed symbols just stop linking (never 404): the map
  is rebuilt from the tag file on every docs build.
- **Targets:** `welder-docs` (Doxygen → `<bin>/docs/reference`, *then*
  `mkdocs build` → `<build>/docs/site/index.html`, hooks grafting + auto-linking
  the reference; order matters, so both are steps of one target);
  `welder-docs-serve` (Doxygen once, then `mkdocs serve` — the guide live-reloads,
  the reference is a static snapshot that *is* served, no 404). Both self-skip
  (with a warning) if `doxygen`/`uv` are absent. Graphviz
  (`dot`) is optional (class graphs). NB mkdocs mermaid diagrams: don't hardcode
  node `fill`/`color` (Material flips the label color per theme and htmlLabels
  ignore per-node `color`, giving unreadable text in dark mode) — use `stroke`
  accents and let Material's amber-accent default fill stand.

```bash
cmake --preset welder-gcc16 -DWELDER_BUILD_DOCS=ON   # configure: syncs uv docs env, fetches doxygen-awesome
cmake --build --preset welder-gcc16 --target welder-docs
# open build/welder-gcc16/docs/site/index.html   (guide);  .../site/api/index.html (C++ reference)
cmake --build --preset welder-gcc16 --target welder-docs-serve   # live-reload the guide while writing prose
```
