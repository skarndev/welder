<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/content/assets/welder-lockup-dark.svg">
    <img alt="welder" src="docs/content/assets/welder-lockup-light.svg" width="340">
  </picture>
</div>

[![Linux](https://github.com/skarndev/welder/actions/workflows/ci-linux.yml/badge.svg)](https://github.com/skarndev/welder/actions/workflows/ci-linux.yml)
[![macOS](https://github.com/skarndev/welder/actions/workflows/ci-macos.yml/badge.svg)](https://github.com/skarndev/welder/actions/workflows/ci-macos.yml)
[![Windows](https://github.com/skarndev/welder/actions/workflows/ci-windows.yml/badge.svg)](https://github.com/skarndev/welder/actions/workflows/ci-windows.yml)
[![Docs](https://github.com/skarndev/welder/actions/workflows/docs.yml/badge.svg)](https://github.com/skarndev/welder/actions/workflows/docs.yml)
[![Docs site](https://img.shields.io/badge/docs-skarndev.github.io%2Fwelder-blue)](https://skarndev.github.io/welder/)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue)](https://en.cppreference.com/w/cpp/26)

**Generate language bindings for annotated C++ types straight from C++26
reflection — no external code generator, no parsing step.**

welder is a header-only **C++26** library that reads [P2996][p2996] reflection and
[P3394][p3394] annotations *at compile time* to emit binding-registration code for
your types (e.g. [pybind11] `class_<T>` calls) directly, through template
instantiation. You mark a type with attributes saying *which languages* it should be
exposed to and *which members* participate; welder reflects over it and lays the
bindings down. On top of that it carries the reflected documentation into the target
language — Python `.pyi` stubs and Lua LuaCATS stubs — so IDE autocompletion and
static analysis come along for free.

> **Status: early proof-of-concept.** Verified end-to-end (an importable Python
> module; a `require`-able Lua module), but the API is still moving and **gcc-16 is
> the only compiler** that implements the papers it needs. Targets **C++26 and newer
> only**.

## Supported languages

welder emits through a **rod** — a small policy struct for one binding backend. The
same annotated type binds through any rod you weld it for:

| Language | Backend | Output |
|---|---|---|
| Python | [pybind11] | extension module + `.pyi` typing stubs |
| Python | [nanobind] | extension module + `.pyi` typing stubs |
| Python | trampolines | build-time `.hpp` of virtual-override trampolines (serves both Python rods) |
| Lua | [sol2] | loadable module |
| Lua | [LuaBridge3] | loadable module |
| Lua | [LuaCATS] | build-time `---@meta` stub file |

Adding a language is one rod struct; the language-agnostic core is reused verbatim.
Further backends are designed-for but not yet implemented.

## The idea

Annotate the C++ type — say which languages, which members, and the docs — then let
welder lay the bindings down:

```cpp
#include <welder/vocabulary.hpp>            // annotation vocabulary (header-only)
#include <pybind11/pybind11.h>
#include <welder/rods/python/pybind11/rod.hpp>
#include <welder/rods/python/naming.hpp>    // welder::rods::python::pep8

struct [[=welder::weld(welder::lang::py, welder::lang::lua),   // expose to py + lua
         =welder::policy::automatic,                           // reflect all members
         =welder::doc("An axis-aligned rectangle.")]]
Rectangle {
    double width{0.0};
    double height{0.0};

    [[=welder::mark::exclude]] std::uint64_t cacheHandle{0};   // internal, bound nowhere

    [[=welder::doc(R"(
        Compute the area of the rectangle.

        Width and height are treated as unsigned extents; a
        degenerate (zero) side yields zero area.)"),
      =welder::returns("The area in square units.")]]
    double computeArea() const { return width * height; }
};

PYBIND11_MODULE(shapes, m) {
    using welder::rods::pybind11::rod;
    using welder::rods::python::google_style;   // Google-style docstrings (Args:/Returns:)
    using welder::rods::python::pep8;            // PEP 8 names: computeArea → compute_area
    welder::welder<rod<google_style>, pep8>::weld_type<Rectangle>(m);
}
```

The compiled module carries the renamed method, the folded docstring, and *not* the
excluded member:

```pycon
>>> import shapes
>>> r = shapes.Rectangle()
>>> r.width, r.height = 3.0, 4.0
>>> r.compute_area()                          # C++ computeArea(), renamed to PEP 8
12.0
>>> print(shapes.Rectangle.compute_area.__doc__)
compute_area(self: shapes.Rectangle) -> float

Compute the area of the rectangle.

Width and height are treated as unsigned extents; a
degenerate (zero) side yields zero area.

Returns:
    The area in square units.
>>> hasattr(r, "cacheHandle")                 # excluded member — never bound
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
not convert your types for you (that stays the framework's job — a [pybind11]
`type_caster`, a [nanobind] caster, a [sol2] usertype), it does not replace the
binding framework, and it does not flatten the languages into one
lowest-common-denominator API.

## Quick start

Building welder's own examples and tests from a clone uses **Conan 2** to provision
the backends ([pybind11] / [nanobind] / [sol2]). It also requires **gcc-16** (GCC ≥ 16.1 —
the only compiler with P2996 + P3394, installed from whatever package manager or
source build you prefer) and CMake ≥ 3.28 + Ninja. (Consuming welder in *your* project
needs neither Conan nor the backends — see [Consuming welder](#consuming-welder).)

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

See the [getting-started guide](https://skarndev.github.io/welder/guide/getting-started/)
for the full walkthrough.

## Consuming welder

welder is header-only and exports the *core only* — `welder::headers` is just the
include path. You bring your own backend ([pybind11] / [nanobind] / [sol2] /
[LuaBridge3]) and, on your own target, set C++26 + gcc's `-freflection`. welder does
**not** force the standard or the flag onto your target; it *checks* them and fails
with a clear message if they're missing, rather than imposing them.

### Obtaining welder

**CMake — FetchContent** (no install step; as a subproject welder builds *nothing* of
its own — no backends, tests or install rules — so it only needs a C++26 compiler):

```cmake
include(FetchContent)
FetchContent_Declare(welder
  GIT_REPOSITORY https://github.com/skarndev/welder.git
  GIT_TAG main)
FetchContent_MakeAvailable(welder)                     # defines welder::headers
```

**CMake — install + `find_package`** (nothing of welder's own compiles, so disable the
dev-time build and install just the header tree):

```bash
cmake -S welder -B build \
  -DWELDER_BUILD_EXAMPLES=OFF -DWELDER_BUILD_TESTS=OFF \
  -DWELDER_BUILD_PYBIND11=OFF -DWELDER_BUILD_NANOBIND=OFF \
  -DWELDER_BUILD_SOL2=OFF -DWELDER_BUILD_LUABRIDGE=OFF
cmake --install build --prefix /your/prefix
```

**Conan** (optional — only if your project already uses Conan). welder ships a recipe;
build and publish it to your local cache, then `requires("welder/0.1.0")` downstream:

```bash
conan create . -pr:a conan/profiles/gcc16 --build=missing   # → local ~/.conan2 cache
```

GitHub Packages doesn't host Conan, so there's no public remote yet — the local cache
is the current channel.

### Wiring it onto your target

However you obtain welder, the target wiring is the same — here linking [nanobind]
for a Python extension:

```cmake
find_package(welder REQUIRED)                          # welder::headers + build helpers
find_package(Python 3.12 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(nanobind CONFIG REQUIRED)                 # your backend, however you provide it

nanobind_add_module(mymod src/bindings.cpp)            # your extension module
target_link_libraries(mymod PRIVATE welder::headers)   # welder = the include path
target_compile_features(mymod PRIVATE cxx_std_26)      # welder needs C++26 …
target_compile_options(mymod PRIVATE -freflection)     # … + gcc-16's reflection flag
```

`find_package(welder)` (and a FetchContent pull) also define the build helpers —
`welder_pybind11_generate_stubs`, `welder_sol2_add_module`, … — for emitting the
loadable module and its stubs. (With FetchContent, drop the `find_package(welder)`
line — `welder::headers` is already defined by `FetchContent_MakeAvailable`.)

## Documentation

The full documentation lives at
**[skarndev.github.io/welder](https://skarndev.github.io/welder/)** — a
[mkdocs-material](https://squidfunk.github.io/mkdocs-material/) guide plus a
Doxygen-generated C++ reference, rebuilt and published on every push. Highlights:

- [Getting started](https://skarndev.github.io/welder/guide/getting-started/) ·
  [Annotation vocabulary](https://skarndev.github.io/welder/guide/annotations/)
- [Binding a type](https://skarndev.github.io/welder/guide/binding-types/) ·
  [Enums](https://skarndev.github.io/welder/guide/enums/) ·
  [Inheritance](https://skarndev.github.io/welder/guide/inheritance/) ·
  [Namespaces & modules](https://skarndev.github.io/welder/guide/namespaces-modules/)
- [Docstrings](https://skarndev.github.io/welder/guide/docstrings/) ·
  [Naming conventions](https://skarndev.github.io/welder/guide/naming/)
- [The bindability gate](https://skarndev.github.io/welder/guide/bindability/) ·
  [Trust & type casters](https://skarndev.github.io/welder/guide/trust-casters/)
- [Architecture](https://skarndev.github.io/welder/architecture/) ·
  [C++ API reference](https://skarndev.github.io/welder/api/)

## License

[MIT](LICENSE) © 2026 Sergey Shumakov

[p2996]: https://wg21.link/p2996
[p3394]: https://wg21.link/p3394
[pybind11]: https://github.com/pybind/pybind11
[nanobind]: https://github.com/wjakob/nanobind
[sol2]: https://github.com/ThePhD/sol2
[LuaBridge3]: https://github.com/kunitoki/LuaBridge3
[LuaCATS]: https://luals.github.io/wiki/annotations/