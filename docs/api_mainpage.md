# welder — C++ API reference {#mainpage}

This is the **generated C++ reference** for welder: every class, template, concept,
namespace and file under `src/welder/` — public API *and* `detail/` internals — read
straight from the real headers through welder's own
[Doxygen INPUT_FILTER](https://github.com/skarndev/welder/blob/main/tools/welder_doxygen_filter.py),
so the `[[=welder::doc/returns/tparam]]` annotations come through.

> **New to welder?** Read the narrative **Guide** first — it explains *why* each
> piece exists, with runnable examples. This reference is the exhaustive *what*.

## Where to start

- **The entry point** — `welder::welder<Rod, Style, Carriage>` (`welder.hpp`):
  `weld_type`, `weld_function`, `weld_variable`, `weld_namespace`,
  `weld_namespace_as_submodule` and `weld_module`, each a one-line forward to the
  carriage.
- **The carriage** — the injectable traversal driver,
  `welder::carriages::basic_carriage<Resolution>` (`carriage.hpp`), shipped in two
  flavors: `welder::stitch_welding_carriage` (bind only where welder's markers
  direct — the default) and `welder::tack_welding_carriage` (bind an unmarked
  library greedily). The injected `Resolution` policy decides which entities
  participate; the carriage owns the walk (`bind_type`, `bind_enum`,
  `bind_function`, `bind_variable`, `bind_namespace`, `build_module`).
- **The interface concepts** — `concepts.hpp`: the emission contract every rod
  satisfies (`welder::rod`), plus the `caster_oracle`, `resolution`, `doc_style`
  and `naming::name_style` customization-point contracts, pooled in one catalogue.
- **The reflection layer** — `reflect.hpp` (`welded_for`, `member_bound`,
  `public_bases`), `bind_traits.hpp` (what binds), `bindable.hpp` (the bindability
  gate), `doc.hpp` (docstring folding), `naming.hpp` (the name styles).
- **The vocabulary** — `lang.hpp`, `annotations.hpp` (kept std-include-free, so a
  future `import welder;` module wrapper can re-export exactly these).
- **The rods** — one struct per backend, `welder::rods::<name>::rod`, four runtime
  and two build-time:

| Rod | Header | Emits |
|---|---|---|
| `welder::rods::pybind11::rod<>` | `rods/python/pybind11/rod.hpp` | Python (pybind11) registration |
| `welder::rods::nanobind::rod<>` | `rods/python/nanobind/rod.hpp` | Python (nanobind) registration |
| `welder::rods::sol2::rod` | `rods/lua/sol2/rod.hpp` | Lua (sol2) registration |
| `welder::rods::luabridge::rod` | `rods/lua/luabridge/rod.hpp` | Lua (LuaBridge3) registration |
| `welder::rods::luacats::rod` | `rods/lua/luacats/rod.hpp` | a LuaCATS `---@meta` stub file (build time) |
| `welder::rods::trampolines::rod` | `rods/python/trampolines/rod.hpp` | a pybind11/nanobind trampoline header (build time) |

## How this reference is built

welder's headers carry `/** … */` Doxygen comments, and the `[[=welder::doc]]`
annotations flow in through the same filter that powers the runtime docstrings.
The reference is generated with `EXTRACT_ALL` so the full structure — templates,
internal helpers, class relationships — is browsable, with a source browser
throughout. See the guide's *Generating C++ docs* page for the filter's design and
fail-safety contract.