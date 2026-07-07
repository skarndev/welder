# Languages

welder binds your annotated C++ to **Python** and **Lua** today. The piece that
turns welder's reflection into a given framework's real registration calls is a
**rod** (a welding rod) — Python has two (pybind11 and nanobind), Lua has two
(sol2 and LuaBridge3), plus a build-time rod that writes a Lua editor stub. The
[guide](../guide/index.md) is deliberately rod-agnostic — the annotations, the
resolution rule, inheritance, and the bindability gate are all shared core. This
section covers what changes *per rod*: the exact `weld_type` call, the framework it
targets, and the small surface where languages genuinely differ.

## The rods welder ships

Each rod is one struct, `welder::rods::<name>::rod`, driven through the shared entry
point `welder::welder<Rod>`:

| Rod | Type | Language | Target framework | Entry symbol |
|---|---|---|---|---|
| **pybind11** | `welder::rods::pybind11::rod` | Python | [pybind11](https://pybind11.readthedocs.io/) | `PyInit_<name>` |
| **nanobind** | `welder::rods::nanobind::rod` | Python | [nanobind](https://nanobind.readthedocs.io/) | `PyInit_<name>` |
| **sol2** | `welder::rods::sol2::rod` | Lua | [sol2](https://sol2.readthedocs.io/) | `luaopen_<name>` |
| **luabridge** | `welder::rods::luabridge::rod` | Lua | [LuaBridge3](https://github.com/kunitoki/LuaBridge3) | `luaopen_<name>` |
| **luacats** | `welder::rods::luacats::rod` | *(build-time)* | LuaCATS `---@meta` stub | — |

The first four are **runtime** rods: each emits registration code so an
importable/`require`-able module exists at run time. `welder::rods::luacats::rod` is
a **build-time** rod — it walks the same welded Lua types through the same driver
and writes a [LuaCATS stub file](lua.md#stubs-luacats) instead of runtime code (the
Lua analogue of Python's `.pyi` stubs).

All of them plug into the *same* core:
`welder::welder<Rod>::weld_type<T>(m)`, `weld_namespace<^^ns>(m)`, and the
[`WELDER_MODULE`](../guide/namespaces-modules.md#binding-a-whole-module) entry macro
have identical shapes; only the module handle type and the emitted surface differ.
(The rod's CMake target keeps its framework spelling — `welder::pybind11` etc.)

## Which one should I pick?

- **Python, and you want the broadest feature coverage** → **pybind11**. Multiple
  inheritance, overload dispatch, live namespace variables — the reference rod.
- **Python, and you want small, fast extensions** → **nanobind**. Nearly a drop-in
  for pybind11 in welder (same `weld_type` shape, same docstring styles), with a few
  [documented trade-offs](python.md#feature-comparison) (single inheritance).
- **Lua** → **sol2** or **LuaBridge3** — both bind the same welded C++ and run the
  same tests. Pick **sol2** for the widest feature coverage (virtual inheritance);
  pick **LuaBridge3** if you want a dependency-free header, a different license, or a
  **newer Lua** (LuaBridge3 supports 5.5 and LuaJIT/Luau; sol2 caps at 5.4). Either
  way, add the **luacats** stub generator for editor support.

You are not locked in: because each language has its own entry symbol, one shared
object can expose *several* rods at once. See
[Shipping multiple rods](multiple.md).

<div class="grid cards" markdown>

-   **[Python (pybind11 & nanobind)](python.md)** — the two Python rods and a
    feature-by-feature comparison.
-   **[Lua (sol2)](lua.md)** — the loadable Lua C module, metamethods, enums as
    tables, and the LuaCATS stub.
-   **[Lua (LuaBridge3)](luabridge.md)** — the second Lua rod: same welded C++, a
    dependency-free header, newer Lua support, and how it differs from sol2.
-   **[Shipping multiple rods](multiple.md)** — CMake for building the same
    module against more than one rod.

</div>
