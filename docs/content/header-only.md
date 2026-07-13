# Header-only for now

welder ships **header-only**. A consuming translation unit brings the annotation
vocabulary in with a single include, then the rod header:

```cpp
#include <welder/vocabulary.hpp>              // the vocabulary
#include <welder/rods/python/pybind11/rod.hpp> // a rod (pulls in the core)
```

There is deliberately **no `import welder;` C++20 module** today. welder was
designed Boost-style — a header-only core with one optional module wrapper that
re-exports the std-free vocabulary — and that wrapper *worked*, but it was removed
until the toolchain around it stabilises. This page explains why, and what has to
change for it to come back.

## Why not a module yet

### 1. Only one compiler implements the language welder needs

welder is built on two very recent papers — **P2996** (reflection) and **P3394**
(annotations). Today **gcc-16 is the only compiler that implements both**:

| Compiler | P2996 + P3394 | C++20 modules |
|---|---|---|
| **gcc-16** | yes (experimental, `-freflection`) | yes, but see below |
| Clang | experimental fork only, not in a release | mature |
| MSVC | none yet | mature |

A module wrapper that only one compiler can consume — and only in an experimental
mode — is not a portable delivery form. Header-only, by contrast, works anywhere the
reflection frontend does, and will keep working unchanged as Clang and MSVC catch
up. Shipping the module wrapper only makes sense once *more than one* toolchain can
actually build against it.

### 2. On gcc-16, `-freflection` and modules actively conflict

Even restricting attention to gcc-16, the module path hits a compiler bug that
directly affects welder's real workload. A single translation unit that combines all
three of —

1. `-freflection` (which every welder TU needs),
2. an imported module whose interface pulls in the standard library — either
   `import std;` or an imported module that includes std in its purview, and
3. textual standard-library includes (which **pybind11**, **Python.h**, and the
   platform SDK all do),

— fails to compile with `error: conflicting imported declaration '__mbstate_t'`
(and siblings like `std::streampos`). Drop `-freflection` and the exact same code
compiles: reflection is the amplifier that breaks the otherwise-working merge of a
BMI's std with textual std.

welder's vocabulary module was carefully kept **std-free** precisely to dodge this —
and a std-free vocabulary module *does* survive. But the margin is thin: the moment
anything on the module side touches std, every pybind11 consumer breaks, and the
surrounding `import std;` / module-scanning machinery on this toolchain is still
fragile (sol2's `<luaconf.h>`, for instance, does not survive C++20 module
dependency scanning at all, so a Lua binding TU could never use the module form
regardless). The header-only path has none of these failure modes.

#### Upstream status

The relevant bugs are already reported and triaged upstream — no new report is
needed:

- **[PR124919](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=124919)** — *"Using
  `-freflection` together with `import std` and `#include` causes an ICE."* Closed
  as a duplicate of **[PR99000](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99000)**
  (*"[modules] merging of textual redefinitions"*), which is still **open**.
- **[PR123810](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=123810)** — the
  `__mbstate_t` conflict itself, from `-freflection` representing typedefs to
  unnamed types (`typedef union {…} __mbstate_t;`, as on Darwin). **Fixed in trunk**
  and backported to `releases/gcc-16`, but not yet in the released 16.1.0 bottle
  most people install.

## Header-only and ODR: the ABI inline namespace

Header-only has one classic failure mode. Every consuming TU instantiates its own
(weak) copy of the welder templates it uses; when two libraries built against
*different* welder versions end up linked into one binary, the linker silently
merges those symbols across versions — an ODR violation, and undefined behavior
that surfaces as subtly wrong bindings in one of the two.

welder guards against that the way fmt and Abseil do: every `welder::` name
actually lives in a versioned **inline namespace** — `namespace welder::inline v0
{ … }` throughout the headers. Inline, so it is invisible in source (you spell
`welder::weld`, `welder::welder<Rod>` as ever), but it *is* part of the mangled
symbol names — so the two libraries above keep distinct symbol sets, each runs the
welder it was built with, and passing welder types between them fails loudly
(different types) instead of corrupting silently. The namespace is bumped only on
ABI-breaking releases.

The version itself lives in `<welder/version.hpp>` (std-include-free, no
reflection use): `WELDER_VERSION_MAJOR` / `MINOR` / `PATCH`, a comparable
`WELDER_VERSION`, `WELDER_VERSION_STRING`, and `WELDER_ABI_NAMESPACE`. It is the
single source of truth — CMake and the Conan recipe parse it.

## What has to change for the module to return

We plan to reintroduce the `import welder;` wrapper. It becomes worthwhile once:

- a released gcc-16 carries the **PR123810** backport *and* the underlying textual
  merge issue (**PR99000**) is resolved, so `-freflection` and modules coexist for a
  real pybind11/Python TU; **and**
- at least one other mainstream compiler (**Clang** or **MSVC**) implements P2996 +
  P3394, so a module is a portable delivery form rather than a single-compiler
  experiment.

The codebase is already shaped for that day: the vocabulary headers
(`lang.hpp`, `annotations.hpp`) are kept **std-include-free**, so they can be
re-exported by a module interface unit unchanged, while everything that touches
`<meta>` (reflection and the rods) stays header-only — exactly the boundary a module
wrapper would draw. See the [architecture page](architecture.md#header-only-and-the-vocabulary-boundary)
for that split.

Until then: `#include <welder/vocabulary.hpp>`, and everything in the guide works
the same way.