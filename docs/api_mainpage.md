# welder — C++ API reference {#mainpage}

This is the **generated C++ reference** for welder: every class, template, concept,
namespace and file under `src/welder/` — public API *and* `detail/` internals — read
straight from the real headers through welder's own
[Doxygen INPUT_FILTER](https://github.com/skarndev/welder/blob/main/tools/welder_doxygen_filter.py)
so the `[[=welder::doc/returns/tparam]]` annotations come through.

> **New to welder?** Read the narrative **Guide** first — it explains *why* each
> piece exists, with runnable examples. This reference is the exhaustive *what*.

## Where to start

- **The `welder::welder<Rod>` entry point** (`welder.hpp`) — the shared entry point
  (`weld_type`, `weld_function`, `weld_variable`, `weld_namespace`,
  `weld_namespace_as_submodule`, `weld_module`), each a one-line forward to the
  injectable traversal driver — the **carriage** (`basic_carriage<Resolution>` in
  `carriage.hpp`, shipped as `welder::stitch_welding_carriage` and
  `welder::tack_welding_carriage`: `bind_type`, `bind_enum`, `bind_function`,
  `bind_variable`, `bind_namespace`, `build_module`).
- **The interface concepts** (`concepts.hpp`) — the emission contract every rod
  satisfies (`welder::rod`) plus the `caster_oracle`, `doc_style` and
  `naming::name_style` customization-point contracts, pooled in one catalogue.
- **The reflection layer** — `reflect.hpp` (`welded_for`, `member_bound`,
  `public_bases`), `bind_traits.hpp` (what binds), `bindable.hpp` (the bindability
  gate), `doc.hpp` (docstring folding).
- **The vocabulary** — `lang.hpp`, `annotations.hpp` (kept std-free, so a future
  `import welder;` module wrapper can re-export exactly these).
- **The pybind11 rod** — `rods/python/pybind11/rod.hpp` (`welder::rods::pybind11::rod<>`).

## How this reference is built

welder's headers carry `/** … */` Doxygen comments, and the `[[=welder::doc]]`
annotations flow in through the same filter that powers the runtime docstrings.
The reference is generated with `EXTRACT_ALL` so the full structure — templates,
internal helpers, class relationships — is browsable, with a source browser
throughout. See the guide's *Generating C++ docs* page for the filter's design and
fail-safety contract.
