# welder

**Generate language bindings for annotated C++ types straight from C++26
reflection — no external code generator, no parsing step.**

welder is a header-only **C++26** library that reads [P2996][p2996] reflection and
[P3394][p3394] annotations *at compile time* to emit binding-registration code for
your types (e.g. pybind11 `class_<T>` calls) directly, through template
instantiation. You mark a type with attributes saying *which languages* it should be
exposed to and *which members* participate; welder reflects over it and lays the
bindings down. On top of that it carries the reflected documentation into the target
language — Python `.pyi` stubs and Lua LuaCATS stubs — so IDE autocompletion and
static analysis come along for free.

> **Status: early proof-of-concept.** Verified end-to-end (an importable Python
> module; a `require`-able Lua module), but the API is still moving and **gcc-16 is
> the only compiler** that implements the papers it needs. Targets **C++26 and newer
> only**.

[p2996]: https://wg21.link/p2996
[p3394]: https://wg21.link/p3394

## The idea

```cpp
#include <welder/vocabulary.hpp>            // annotation vocabulary (header-only)
#include <pybind11/pybind11.h>
#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]  // expose to py + lua
       [[=welder::policy::automatic]]                          // reflect all members
Point {
    double x{0.0};
    double y{0.0};
    [[=welder::mark::exclude]] std::uint64_t internal_id{0};   // bound nowhere
};

PYBIND11_MODULE(shapes, m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Point>(m);
}
```

```pycon
>>> import shapes
>>> p = shapes.Point(); p.x = 1.5
>>> p.x
1.5
>>> hasattr(p, "internal_id")
False
```

The *same* annotated type binds to every language you weld it for — you write it
once.

## Why welder?

- **No codegen step.** The bindings *are* the compile. welder reads reflection +
  annotations in-process — no `.i` files, no generator to run, no parser to keep in
  sync with your headers.
- **A tiny vocabulary.** `weld`, `policy`, `mark`, `doc`, `returns`, `tparam`,
  `weld_as`. Say what binds and to which languages; welder resolves the rest at
  compile time.
- **One annotation, several audiences.** A `doc` becomes the Python `__doc__`, the
  Lua LuaCATS stub, *and* — via a Doxygen filter — the C++ reference. Write it once.
- **Idiomatic names per language.** An injectable name-style transformer coerces
  your C++ house style into the target's convention (e.g. PEP 8 for Python), with a
  `weld_as` escape hatch for the cases a rule can't capture.
- **Fail-safe by contract.** Every surface welder is about to bind must be
  representable — otherwise a **hard compile error** naming the offending type, never
  a silent skip.

welder removes boilerplate; it is **not** a universal binding abstraction. It does
not convert your types for you (that stays the framework's job — a pybind11
`type_caster`, a nanobind caster, a sol2 usertype), it does not replace the binding
framework, and it does not flatten the languages into one lowest-common-denominator
API.

## How it fits together

A language-agnostic **core** owns the reflection work — deciding *what* binds,
whether each type is *representable*, and walking types/namespaces/bases. A **rod**
(a welding rod, `welder::rods::<name>::rod`) is a stateless policy struct supplying
only the emission primitives for one backend, driven through the single entry point
`welder::welder<Rod>`. Adding a language is one rod struct; the core is reused
verbatim.

| Rod | Language | Kind |
|---|---|---|
| `welder::rods::pybind11::rod` | Python (pybind11) | runtime module |
| `welder::rods::nanobind::rod` | Python (nanobind) | runtime module |
| `welder::rods::sol2::rod` | Lua (sol2) | runtime module |
| `welder::rods::luabridge::rod` | Lua (LuaBridge3) | runtime module |
| `welder::rods::luacats::rod` | Lua (LuaCATS `---@meta` stub) | build-time stub |

The Python rods additionally emit `.pyi` typing stubs. Further languages are
designed-for but not yet implemented.

## Delivery model

**Header-only** (`src/welder/…`). The vocabulary arrives via `#include
<welder/vocabulary.hpp>`; a rod pulls in the core itself (`#include
<welder/rods/python/pybind11/rod.hpp>`). The optional `import welder;` module wrapper
is removed until the gcc-16 modules bugs are fixed and another toolchain implements
P2996 — see `docs/content/guide/header-only.md`.

## Quick start

Requires **gcc-16** (GCC ≥ 16.1 — the only compiler with P2996 + P3394, installed
from whatever package manager or source build you prefer), CMake ≥ 3.28 + Ninja, and
Conan 2.

```bash
conan install . -pr:a conan/profiles/gcc16 --build=missing
cmake --preset welder-gcc16
cmake --build --preset welder-gcc16
```

The example modules are then loadable, both built from the same welder core:

```bash
# Python
PYTHONPATH=build/welder-gcc16/examples/python_poc \
  python3 -c "import welder_poc as w; p=w.Point(); p.x=1.5; print(p.x)"

# Lua
LUA_CPATH='build/welder-gcc16/examples/lua_poc/?.so' \
  lua -e 'local s=require("shapes_lua"); local r=s.Rect(3,4); print(r:area())'
```

See [`docs/content/guide/getting-started.md`](docs/content/guide/getting-started.md)
for the full walkthrough.

## Documentation

The user guide and C++ reference live under [`docs/`](docs/) (an mkdocs-material site
with a Doxygen-generated reference). Highlights:

- [Getting started](docs/content/guide/getting-started.md)
- [Annotation vocabulary](docs/content/guide/annotations.md)
- [Binding a type](docs/content/guide/binding-types.md) ·
  [Enums](docs/content/guide/enums.md) ·
  [Inheritance](docs/content/guide/inheritance.md) ·
  [Namespaces & modules](docs/content/guide/namespaces-modules.md)
- [Docstrings](docs/content/guide/docstrings.md) ·
  [Naming conventions](docs/content/guide/naming.md)
- [The bindability gate](docs/content/guide/bindability.md) ·
  [Trust & type casters](docs/content/guide/trust-casters.md)
- [Architecture](docs/content/architecture.md)

## Repository layout

| Path | What |
|---|---|
| `src/welder/` | the header-only library (vocabulary, core, rods) |
| `examples/` | loadable Python & Lua proof-of-concept modules |
| `tests/` | backend-neutral C++ cases each rod binds and asserts (pytest / busted) |
| `docs/` | the guide + Doxygen reference site |

## License

[MIT](LICENSE) © 2026 Sergey Shumakov