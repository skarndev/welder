# Backends

A **backend** is what turns welder's reflection into real registration calls in a
target framework. The [guide](../guide/index.md) is deliberately backend-agnostic —
the annotations, the resolution rule, inheritance, and the bindability gate are all
shared core. This section covers what changes *per backend*: the exact `bind` call,
the framework it targets, and the small surface where languages genuinely differ.

## The backends welder ships

| Backend | `welder::…` | Language | Target framework | Entry symbol |
|---|---|---|---|---|
| **pybind11** | `welder::pybind11` | Python | [pybind11](https://pybind11.readthedocs.io/) | `PyInit_<name>` |
| **nanobind** | `welder::nanobind` | Python | [nanobind](https://nanobind.readthedocs.io/) | `PyInit_<name>` |
| **sol2** | `welder::sol2` | Lua | [sol2](https://sol2.readthedocs.io/) | `luaopen_<name>` |
| **luacats** | `welder::luacats` | *(build-time)* | LuaCATS `---@meta` stub | — |

The first three are **runtime** backends: each emits registration code so an
importable/`require`-able module exists at run time. `welder::luacats` is a
**build-time** backend — it walks the same welded Lua types through the same driver
and writes a [LuaCATS stub file](lua.md#stubs-luacats) instead of runtime code (the
Lua analogue of Python's `.pyi` stubs).

All of them plug into the *same* core: `welder::<backend>::bind<T>(m)`,
`bind_namespace<^^ns>(m)`, and the [`WELDER_MODULE`](../guide/namespaces-modules.md#binding-a-whole-module)
entry macro have identical shapes; only the module handle type and the emitted
surface differ.

## Which one should I pick?

- **Python, and you want the broadest feature coverage** → **pybind11**. Multiple
  inheritance, overload dispatch, live namespace variables — the reference backend.
- **Python, and you want small, fast extensions** → **nanobind**. Nearly a drop-in
  for pybind11 in welder (same `bind` shape, same docstring styles), with a few
  [documented trade-offs](python.md#feature-comparison) (single inheritance).
- **Lua** → **sol2**, plus the **luacats** stub generator for editor support.

You are not locked in: because each language has its own entry symbol, one shared
object can expose *several* backends at once. See
[Shipping multiple backends](multiple.md).

<div class="grid cards" markdown>

-   **[Python (pybind11 & nanobind)](python.md)** — the two Python backends and a
    feature-by-feature comparison.
-   **[Lua (sol2)](lua.md)** — the loadable Lua C module, metamethods, enums as
    tables, and the LuaCATS stub.
-   **[Shipping multiple backends](multiple.md)** — CMake for building the same
    module against more than one backend.

</div>
