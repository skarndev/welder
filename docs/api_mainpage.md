# welder — C++ API reference {#mainpage}

This is the **generated C++ reference** for welder: every class, template, concept,
namespace and file under `src/welder/` — public API *and* `detail/` internals — read
straight from the real headers through welder's own
[Doxygen INPUT_FILTER](https://github.com/) so the `[[=welder::doc/returns/tparam]]`
annotations come through.

> **New to welder?** Read the narrative **Guide** first — it explains *why* each
> piece exists, with runnable examples. This reference is the exhaustive *what*.

## Where to start

- **The `welder::backend` concept** (`backend.hpp`) — the emission contract every
  backend satisfies, plus the generic driver (`bind_type`,
  `bind_namespace_driver`, `build_module_driver`).
- **The reflection layer** — `reflect.hpp` (`welded_for`, `member_bound`,
  `public_bases`), `bind_traits.hpp` (what binds), `bindable.hpp` (the bindability
  gate), `doc.hpp` (docstring folding).
- **The vocabulary** — `lang.hpp`, `annotations.hpp` (std-free; the `welder` module
  exports exactly these).
- **The pybind11 backend** — `backends/python/pybind11/backend.hpp`.

## How this reference is built

welder's sources are documented with ordinary comments today; this reference is
generated with `EXTRACT_ALL` so the full structure — templates, internal helpers,
class relationships — is browsable, with a source browser throughout. As Doxygen
`///` comments and `[[=welder::doc]]` annotations are added to the headers, they
flow into these pages automatically (the same filter that powers the runtime
docstrings). See the guide's *Generating C++ docs* page for the filter's design and
fail-safety contract.
