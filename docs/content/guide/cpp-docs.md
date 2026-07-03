# Generating C++ docs

The same `doc` / `returns` / `tparam` annotations that drive runtime docstrings
also document the **C++ API** — from the *real sources*, with no shadow headers to
keep in sync. This is what builds the [C++ API Reference](../reference.md).

## The problem

Doxygen's native parser copes with welder's C++26 sources, but it silently
**discards** `[[=…]]` annotations — and Doxygen has **no plugin system**. Its only
extension point is `INPUT_FILTER`: a per-file program whose stdout is what Doxygen
parses (your files on disk are untouched).

## The filter

`tools/welder_doxygen_filter.py` is that program. It translates welder's vocabulary
into Doxygen comments and strips the rest:

| welder | Doxygen |
|---|---|
| `doc("…")` on a class/function/namespace | `/** … */` |
| `doc("…")` on a parameter | trailing `/**< … */` |
| `returns("…")` | `@return …` |
| `tparam("T", "…")` | `@tparam T …` |
| `weld` / `policy` / `mark` / `trust_bindable` | *stripped* |

Doc **scope** control is Doxygen-native — use `EXCLUDE_SYMBOLS`, not annotations.

## How to wire it

Point Doxygen's `INPUT_FILTER` at the script (it needs a Python with
[`lark`](https://lark-parser.readthedocs.io/)):

```doxyfile
INPUT_FILTER = "python3 /path/to/tools/welder_doxygen_filter.py"
```

welder's own docs build does exactly this — see [the reference](../reference.md),
built by `docs/CMakeLists.txt`, which runs the filter through the uv docs
environment (the interpreter that has `lark`).

## Why a grammar

The filter is built on a two-layer **Lark grammar**
(`tools/welder_doxygen_filter.lark`) sharing one lexer:

- **Layer 1** lexes the C++ *lexical soup*. Comments, string/char literals and raw
  strings are single **atomic tokens**, so annotation-shaped text inside a string or
  a commented-out line is invisible. Lexing is **total** (a one-char catch-all: any
  bytes lex). `<<` / `>>` / `<=` / `>=` / `<=>` / `->` are maximal-munch operators so
  shifts and comparisons are never mistaken for template angles. Line splices are
  honored.
- **Layer 2** parses one annotation block's content: a top-level comma split with
  balanced nested groups; welder specs are translated, non-welder attributes
  (`[[nodiscard]]`, `[[deprecated("…")]]`, foreign annotations) are re-emitted in
  place.

Placement is careful: keyword-position annotations hoist before
`struct`/`class`/`union`/`enum`/`namespace`, before `template <…>` heads, and before
a `requires`-clause; parameter docs become trailing `/**< */` (with the
`<`-template-vs-less-than ambiguity resolved by *tentative matching*, so
`std::map<K,V> x = {…}` and defaults like `flags = 1 << 4` survive);
enumerator/member docs land **after** any initializer.

## Fail-safety contract

A doc build can **never crash** on someone's code. This is locked by a hostile-input
golden test:

- lexing is total (any bytes lex);
- each block transforms in its own `try`/`except` — an unparseable block is emitted
  **verbatim** (with a stderr note);
- a last-resort `try`/`except` (even a missing `lark`) emits the whole file
  unchanged;
- non-UTF-8 survives via `surrogateescape` with byte-exact stdout;
- exit code is **0** in all these cases.

Worst case, a file loses its welder annotations in the docs — never its code.

!!! info "One annotation, both audiences — including templates"

    The filter is textual, so annotations *inside* templates translate like
    anywhere else — even though reflection cannot read an uninstantiated template.
    That's the dedupe story end to end: one annotation on a template feeds the C++
    reference *and*, via instantiation reflection, the bound instantiation's
    runtime docstring.

### Known conventions

- Annotations must be spelled `welder::`-qualified (`::welder::` works; a namespace
  *alias* does not).
- The filter is preprocessor-blind — annotations in macro bodies transform
  textually.
- A *Doxygen*-side limit: a bare unparenthesized `<` comparison in a default
  argument derails Doxygen's own parameter parsing — write `(sizeof(T) < 8)`.

See [the docs strategy](../architecture.md#documentation) for how this all wires
into the site you're reading.
