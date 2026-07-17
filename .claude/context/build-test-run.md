# Build, run & test

Read when: building, running the examples, or working on tests / .pyi stubs.

## Build & run
```bash
conan install . -pr:a conan/profiles/gcc16 --build=missing
cmake --preset welder-gcc16 -DWELDER_LUA_DIR="$(brew --prefix lua@5.4)"
cmake --build --preset welder-gcc16
```

**Version knobs (provider-neutral):** conan supplies the C++ binding-framework headers
(sol2/pybind11/nanobind), but the *language runtimes* come from the system/user via
`find_package`, so a consumer can bring their own without conan. `WELDER_LUA_VERSION`
(default `5.4`) + `WELDER_LUA_DIR` (a Lua install prefix) pick the Lua the sol2
rod builds against and its tests run with; `WELDER_PYTHON_VERSION` (default
`3.14`) feeds `find_package(Python …)` + `uv sync --python`. Point `CMAKE_PREFIX_PATH`
/ these vars at your own installs to override the conan defaults.

The example Python module is then importable:
```bash
PYTHONPATH=build/welder-gcc16/examples/python_poc \
  python3 -c "import welder_poc as w; p=w.Point(); p.x=1.5; print(p.x)"
PYTHONPATH=build/welder-gcc16/examples/python_poc \
  python3 -c "import welder_poc as w; print(hasattr(w.Label(), 'cache'))"  # False
```

The Lua example (`examples/lua_poc` → `shapes_lua.so`) is a `require`-able module
(needs a Lua interpreter with `package.cpath` pointing at its dir):
```lua
package.cpath = "build/welder-gcc16/examples/lua_poc/?.so"
local s = require("shapes_lua")
local r = s.Rect(3.0, 4.0); print(r:area())   -- 12.0
```

## Packaging & consuming (pure CMake; Conan optional)

**Conan is NOT required to consume welder** — plain CMake (FetchContent /
`find_package`) is the primary path; Conan is one option among them and is what CI
uses to provision the *backends* for welder's own examples/tests. Four consumer
routes, all landing on the same `welder::headers` target:

- **FetchContent / add_subdirectory** (lightest, no install): `FetchContent_Declare` +
  `FetchContent_MakeAvailable` then link `welder::headers`. **No flags needed** — the
  dev-time options (`WELDER_BUILD_{PYBIND11,NANOBIND,SOL2,LUABRIDGE,EXAMPLES,TESTS,STUBS}`
  and `WELDER_INSTALL`) now default to `${PROJECT_IS_TOP_LEVEL}` (top-level CMakeLists +
  `src/`), so when welder is a subproject it builds *nothing* of its own: no backend
  `find_package()`, no tests, no install-rule leakage. Top-level (developing welder, or
  CI/Conan configuring the repo directly) they stay ON, unchanged.
- **Install + `find_package`:** configure the repo with the dev-time build off
  (`-DWELDER_BUILD_EXAMPLES=OFF -DWELDER_BUILD_TESTS=OFF -DWELDER_BUILD_PYBIND11=OFF …`
  — nothing of welder's own compiles, and backends aren't installed anyway),
  `cmake --install --prefix …`, then `find_package(welder)` + link `welder::headers`.
- **CPM.cmake** (`CPMAddPackage("gh:skarndev/welder#main")`): CPM wraps FetchContent,
  so it rides the same subproject collapse — verified end-to-end (2026-07-15) with a
  scratch consumer fetching main from GitHub; no CMake changes were needed. Once
  releases are tagged, `@0.1.0` pins one — CPM maps `@version` to the **`v<version>`
  git tag** and cross-checks it against welder's `project()` version (parsed from
  `version.hpp`, so it matches by construction). Tag releases `v*` for this to work.
- **Conan** (optional): see the recipe bullet below.

- **CMake install/export** (`src/CMakeLists.txt`, guarded by `WELDER_INSTALL`, default
  `${PROJECT_IS_TOP_LEVEL}`): installs the `src/welder` header tree to `include/`,
  exports the **one** INTERFACE target a consumer links — `welder::headers`, **the
  include path only** — as `welder-targets.cmake`, and ships the five
  `cmake/Welder*.cmake` build helpers next to the generated `welder-config.cmake`.
  **welder propagates nothing else onto a consumer's target:** no `cxx_std_26` usage
  requirement and no `-freflection` (there is no longer a `welder::flags` target). The
  C++ standard and the reflection flag are the consumer's to set; welder *checks* them
  — `WelderRequirements.cmake`'s `welder_check_requirements()` (compiler is gcc-16;
  `CMAKE_CXX_STANDARD` ≥ 26) is `include()`d + called by both the top-level CMakeLists
  and `welder-config.cmake`, and the `#error` guard in `<welder/lang.hpp>` (keyed on
  `__cpp_impl_reflection`, defined only under `-std=c++26 -freflection`) is the
  compile-time backstop for what CMake can't see (per-target/unset standard, missing
  `-freflection`). welder's OWN build gets `-freflection` via a build-tree-scoped
  `add_compile_options` (top CMakeLists), never reaching a consumer. **Only the core
  is exported — rods are not**: welder is bring-your-own-backend. The config
  `include()`s the helpers, so `find_package(welder)` also defines
  `welder_sol2_add_module`, `welder_pybind11_generate_stubs`, etc. Note `EXPORT_NAME`
  is set on `welder_headers` so it exports as `welder::headers` (not
  `welder::welder_headers`) — the ALIAS name alone doesn't export.
- **Recipe** (`conanfile.py`): `package_type = "header-library"`; `package_id`
  clears settings (one package for all configs). It carries **no backend deps** —
  the backends are `test_requires` (never propagated to a consumer), gated by the
  `with_pybind11`/`with_nanobind`/`with_sol2` options (default **ON**) so a plain
  `conan install .` still provisions them for building welder's *own*
  examples/tests, while a consumer's `requires("welder/0.1.0")` stays core-only.
  `package_info()` sets `cmake_find_mode = "none"` so
  CMakeDeps defers to welder's own shipped config (which carries the helpers + the
  requirements check) rather than synthesizing a thinner one.
- **Build & publish locally:**
  ```bash
  conan create . -pr:a conan/profiles/gcc16 --build=missing   # → local ~/.conan2 cache
  ```
  A downstream project then just `requires("welder/0.1.0")` and consumes the package
  through the same `find_package(welder)` / `welder::headers` wiring as the pure-CMake
  routes. (There is **no** Conan `test_package/` — it was a convention CI never ran, so
  it was removed; the documented consume paths are the reference instead.)
- **GitHub note:** GitHub Packages has **no Conan support**, so there's no
  `conan upload` target there. The eventual "remote on GitHub" path is a Conan 2.4+
  `local-recipes-index` repo (conan-center-index layout, added as a remote from a
  clone) — deferred; local cache is the current publish target.

## The cookbook (examples/cookbook — the FetchContent consumption test)

A **standalone** consumer super-project (own `project()`, NOT add_subdirectory'd
from welder's build — nesting would clash with welder's own targets): it obtains
welder via FetchContent and FetchContent-pins the backends (pybind11 v3.0.1,
nanobind v2.13.0, sol2 v3.5.0, all `OVERRIDE_FIND_PACKAGE` so welder's own
`find_package(<backend>)` calls redirect to them; made available at the cookbook's
top scope BEFORE welder so nanobind's `NB_DIR` is inherited by welder's subdir).
Building it therefore tests the consumer packaging path; CI (Linux, macOS AND
Windows jobs, "Cookbook" step — Windows runs it with `WELDER_BUILD_SOL2=OFF` +
`WELDER_LUABRIDGE_LUA_DIR=<ucrt64>` / 5.5, so LuaBridge3 carries the Lua side
there) redirects the welder fetch to the checkout with
`-DFETCHCONTENT_SOURCE_DIR_WELDER=$PWD`:

```bash
cmake -S examples/cookbook -B build/cookbook -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-16 \
  -DFETCHCONTENT_SOURCE_DIR_WELDER="$PWD" \
  -DWELDER_LUA_DIR="$(brew --prefix lua@5.4)" \
  -DPython_EXECUTABLE="$(brew --prefix python@3.14)/bin/python3.14"
cmake --build build/cookbook && ctest --test-dir build/cookbook --output-on-failure
```

Nine recipes (`01-hello` … `09-custom-traversal`), each a self-contained dir with a
`check.py`/`check.lua` CTest (`cookbook.<name>`), each documented as a page in the
docs **Cookbook** section (`docs/content/cookbook/` — keep recipe ↔ page in sync).
Notables: 05 uses `welder_generate_trampolines`, 07 is nanobind + BOTH Lua rods
(sol2 + LuaBridge3 — both `.so`s answer `require("journal")` from per-rod output
dirs, the same check.lua asserts both, all entry points via the styled
`WELDER_MODULE(ns, rod, WelderType)` form) + LuaCATS + nanobind's bundled `.pyi`
stubgen from one header. Its language-flavored methods via `mark::only`: the py
flavor uses nb::object (nanobind header cross-included everywhere; unused inline
members aren't emitted → no cross-language symbol leakage; the Lua/luacats targets
need the nanobind+Python include paths), the lua flavor is a framework-NEUTRAL
std::function (one language, two rods — a sol2 type would lock it to sol2), with
LuaBridge3 taught the conversion by a `luabridge::Stack<std::function<…>>`
specialization in its own TU (sol2 converts natively; framework glue lives in the
framework's TU). 08 tack-welds an unannotated header
(no hatch needed — greedy_resolution's `counts_as_registered` accepts the types
its own pass registers; see bindability-gate.md). 09 is the custom-traversal
recipe: a `skip_private : greedy_resolution<>` (prunes `detail`/`impl`/underscore
names via member_participates/namespace_participates AND mirrors the rule in
counts_as_registered) injected via `basic_carriage<skip_private>` — the runnable
form of extending.md's resolution section; NB the resolution seam is
namespace-level only (class members gate on member_bound, not the resolution).
`WELDER_LUA_DIR`
unset ⇒ the sol2 half of 07 is skipped (message, not error). The umbrella defines
`cookbook_add_pybind11_module()` (the Python_add_library + hidden-visibility
boilerplate).

Two consumer-path fixes this build flushed out (kept in core): the
pybind11::headers SYSTEM promotion resolves `ALIASED_TARGET` first
(FetchContent'd pybind11 is an ALIAS; conan's is IMPORTED), and `name_of_or`
(naming.hpp) makes the weld_* call-site name override lazy so identifier-less
template instantiations bind (recipe 06; locked by tests/core/naming.cpp).

## Strict warnings
welder's own compiled targets (rod tests, examples, the core compile-only checks)
build under a strict warning set; **library consumers never inherit it**. It lives
on a dedicated `welder_warnings` INTERFACE target (top `CMakeLists.txt`), linked
`PRIVATE` into our targets — deliberately *not* folded into `welder::headers` (a
library must not impose its warning flags on consumers — just as it must not impose the
C++ standard or `-freflection`, which welder checks rather than propagates, see
Packaging — and in-tree third-party code, nanobind's runtime and LuaBridge3, must stay
untouched). The
module helpers (`welder_sol2_add_module`, `welder_luabridge_add_module`,
`welder_luacats_generate_stub`) link it themselves, guarded by
`if(TARGET welder_warnings)` so a *consumer* using those helpers is unaffected. The
negative-compile probes deliberately **don't** link it (their failure must be the
bindability gate, not a warning).

Knobs:
- `WELDER_WARNINGS` (default **ON**) — the strict set: `-Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wold-style-cast
  -Wnon-virtual-dtor -Woverloaded-virtual -Wdouble-promotion -Wformat=2
  -Wimplicit-fallthrough -Wuseless-cast -Wextra-semi -Wmisleading-indentation
  -Wredundant-decls`. Curated to be `-Werror`-clean on gcc-16.
- `WELDER_WERROR` (default **OFF**) — promote those to errors. **All CI jobs turn it
  on** (Linux, macOS, and both Windows/mingw configures — the set is clean on LLP64
  too); left off locally so toolchain drift (new gcc warnings) doesn't block a dev
  build.

**`-Wnull-dereference` is intentionally excluded** — it's a gcc *middle-end* warning
(emitted during optimization), so it ignores `-isystem` suppression and leaks a false
positive from pybind11's dispatcher (`std::copy` in libstdc++) that we cannot fix; it
would make `-Werror` impossible. For the same reason all backend headers are consumed
**SYSTEM** (nanobind/sol2/LuaBridge3 already were; pybind11 is now promoted to SYSTEM
in `src/welder/rods/CMakeLists.txt`) so parse-time third-party warnings don't surface.

**No sanitizers (gcc-16 not stable enough).** `-fsanitize=address/undefined` is
deliberately not wired up. Two independent blockers on gcc-16: (1) UBSan's `null` /
`nonnull-attribute` / `returns-nonnull-attribute` pointer checks are injected into
*constant evaluation*, turning welder's `consteval` name resolution (`naming.hpp`
`name_of`) into a non-constant expression so the TU won't even compile (a per-function
`[[gnu::no_sanitize]]` does *not* suppress it — verified); and (2) running the built
extension modules under the sanitizers crashes the host interpreter. Revisit when the
toolchain matures.

## Lua rod build/test (sol2)
`welder::sol2` needs conan `with_sol2` (for the `sol2` C++ headers). **Lua itself is
NOT from conan** — `src/welder/rods/CMakeLists.txt` finds it with CMake's builtin
`FindLua`, pinned to `WELDER_LUA_DIR` so the user's install (not conan's transitive
`lua`, which shadows the search via `CMAKE_PREFIX_PATH`) provides the headers **and
the library** (both pinned: FindLua hard-fails without LUA_LIBRARIES, and a
keg-only install is invisible to its default search — conan always masked this;
the conan-less cookbook exposed it. welder still never links liblua). Relatedly,
the sol2/luabridge `WELDER_MODULE` macros export `luaopen_<ns>` explicitly
(`WELDER_DETAIL_LUAOPEN_EXPORT`: dllexport / default-visibility): a TU that also
includes nanobind's headers (multi-language shared header) carries explicit
dllexports, which turns OFF MinGW's export-everything default and `require` then
fails with "specified procedure could not be found"; ELF/Mach-O gets
default-visibility pinning against -fvisibility=hidden TUs. A Lua
extension is created with `welder_sol2_add_module(<name> <sources>)`
(cmake/WelderSol2Module.cmake): bare `<name>.so`, host-symbol link model, and
**`CXX_SCAN_FOR_MODULES OFF`** (sol2's `<luaconf.h>` fails p1689 module scanning — a
header-unit macro-visibility issue with `LLONG_MAX`; a Lua TU is header-only
regardless). Gated by `WELDER_BUILD_SOL2` (default ON).

**Lua-version matching (important):** the module is built against `WELDER_LUA_DIR`'s
headers and loads its `lua_*` from the *host* interpreter; Lua has NO cross-minor ABI
compatibility — loading a 5.4-built module into a 5.5 interpreter **segfaults**. So
both must be the same minor. A configure-time guard **hard-errors** if the found Lua
minor ≠ `WELDER_LUA_VERSION`; set `-DWELDER_LUA_DIR="$(brew --prefix lua@5.4)"` (keg,
`brew install lua@5.4`). The runtime interpreter comes from that same prefix — the
busted wrapper (below) bakes it in — so there is no longer a separate interpreter
knob to keep in sync.

**Test framework — busted via luarocks (nothing vendored).** `tests/lua` installs
[busted](https://lunarmodules.github.io/busted/) at *configure time* into a
build-local luarocks tree (`--tree <build>/tests/lua/lua_modules --lua-version
${WELDER_LUA_VERSION} --lua-dir ${WELDER_LUA_DIR}`), the Lua analogue of the `uv
sync` step. Needs `luarocks` (found, or `-DWELDER_LUAROCKS_EXECUTABLE=`) **and**
`WELDER_LUA_DIR`; missing either only *skips* the `luatest.sol2` CTest (the module
still builds as a compile check), exactly like the uv gate. The specs are
`tests/lua/spec/*_spec.lua` (busted `describe`/`it` + luassert, mirroring the `.py`
files); `helper.lua` `require`s the built `welder_test_sol2.so` once. Run with
`--no-auto-insulate` — busted's default per-file insulation would unload and
re-`require` the C module, re-running `luaopen` and corrupting sol2's usertype
registration; one shared instance is the intended model (like the pytest conftest
fixture). `LUA_PATH`/`LUA_CPATH` (set by the CMakeLists) reach `helper.lua` and the
`.so`.

## LuaBridge3 rod build/test (a second Lua runtime rod)
`welder::luabridge` is the LuaBridge3 counterpart of `welder::sol2` — same
loadable-module story (`welder_luabridge_add_module()`, host-symbol link model,
`CXX_SCAN_FOR_MODULES OFF`, header-only consumption). Two things differ:
- **Sourcing (split, no Conan package for LuaBridge3):** the *rod* is consumer-facing
  via `find_package(LuaBridge3)` / `-DWELDER_LUABRIDGE_DIR=<dir with LuaBridge/…>`; the
  *tests/examples* FetchContent a pinned commit (`WELDER_LUABRIDGE_GIT_TAG`) when nothing
  is found, gated by `WELDER_LUABRIDGE_FETCH` (default = `WELDER_BUILD_TESTS`). Both
  resolve to LuaBridge3's own `LuaBridge` INTERFACE target. Provisioning is at the top
  level (before `add_subdirectory(src)`) so the rod target can link it.
- **Its own Lua version:** LuaBridge3 supports Lua 5.1–5.5 + LuaJIT/Luau (sol2/3.5.0
  caps at 5.4), so it has independent knobs `WELDER_LUABRIDGE_LUA_VERSION` /
  `WELDER_LUABRIDGE_LUA_DIR` (defaulting to the sol2 ones, so one Lua install serves
  both). Point them at, e.g., `brew --prefix lua@5.5` to exercise it on a newer Lua
  without touching the sol2 build. The same segfault-guarding minor check applies.

**Known limitation:** LuaBridge3 supports non-virtual multiple inheritance but **not
virtual bases** (its base-cast offset is plain pointer arithmetic that a virtual base
breaks — registering one crashes at load), so the shared *virtual* diamond case is
gated off for it (no `WELDER_TEST_MULTIPLE_INHERITANCE`), like nanobind's single-
inheritance gating; the busted inheritance spec skips the diamond when `Bottom` is absent.

## `.pyi` stub generation
Via [pybind11-stubgen](https://github.com/pybind/pybind11-stubgen) (build-time):
`cmake/WelderPybind11Stubgen.cmake` → `welder_pybind11_generate_stubs(<target>
PYTHON <interp> …)`, a POST_BUILD step (`--exit-code`); gated by `WELDER_BUILD_STUBS`
(default ON). `PYTHON` must import the extension (ABI match) and have stubgen;
welder docstrings flow into the stubs. pybind11-stubgen is pinned to its GitHub
`main` branch (fixes not yet on PyPI; see `tests/pyproject.toml`
`[tool.uv.sources]`). Examples opt in via `-DWELDER_STUBGEN_PYTHON=<interp>`.

## LuaCATS stub generation (the Lua analogue of `.pyi`)
Lua has no runtime docstring slot, so the sol2 rod drops `doc`/`returns` at
load time; their home is a **generated LuaCATS (`---@meta`) definition file**,
emitted by the `welder::rods::luacats::rod` (`src/welder/rods/lua/luacats/rod.hpp`).
Unlike `.pyi` (pybind11-stubgen *imports* the built module), a Lua stub cannot be
scraped from a loaded sol2 usertype, so it is **reflection-emitted at build time**:
the stub rod is a real `welder::rod` that plugs the *same* generic driver
as sol2 but emits LuaCATS text instead of registering a live module — so member
selection, base flattening, policy/marks and the bindability gate are reused
verbatim; only the emission primitives differ. It is pure reflection + text —
**no sol2, no Lua runtime** — so `welder::luacats` is an unconditional INTERFACE
target over `welder::headers`. The one piece sol2 didn't need is the C++→LuaCATS
**type map** (`lua_type_string`): scalars → `integer`/`number`/`boolean`/`string`,
the STL wrappers welder recurses (`vector<T>`→`T[]`, `map<K,V>`→`table<K,V>`,
`optional<T>`→`T?`, smart pointers → the pointee), welded classes/enums → their
dotted Lua name, else `any`. A generator TU uses `WELDER_LUACATS_MAIN(<ns>)` (a
`main()` that writes the stub for namespace `^^ns` to its argv[1] path, else
stdout); `welder_luacats_generate_stub(<name> SOURCES … [OUTPUT …])`
(cmake/WelderLuaCATSStub.cmake) builds that generator and runs it into `<name>.lua`
as an ALL target. Overloaded methods/constructors/free functions render as one
documented `function` + `---@overload fun(…)` lines (the first overload with a doc is
the primary, keeping its `@param`/summary text; the others carry signature only, all
LuaCATS records). A **const** data member gets a `(read-only)` description note —
LuaCATS has no read-only field tag (an open lua-language-server request), so it is
documented, not enforced.

**Stub `---@operator` vs runtime metamethods:** `operator_luacats` (type_map.hpp)
only emits the operators lua-language-server actually models (`vm.OP_*_MAP`:
arithmetic/bitwise + `call`/`len`/`concat`/`unm`/`bnot`). **Comparison (`==`/`<`/
`<=`) and subscript (`[]`) are dropped** — they have no `---@operator` spelling, so
the sol2 runtime binds them (`__eq`/`__lt`/`__le`/`__index`) but the stub can't type
them; emitting `---@operator eq/lt/le/index` makes the language server reject the
stub with `unknown-operator`.

**LuaCATS stubcheck (the Lua analogue of `stubcheck.<variant>` mypy).** The
generated stub is validate-if-present linted by **lua-language-server**: the
`stubcheck.luacats` CTest runs `lua-language-server --check` over it and gates on its
exit code (0 = clean), gated on `find_program(lua-language-server)` — a missing
server only *skips* the lint (the byte-exact golden still gates), exactly like the
uv/luarocks gates. The stub is emitted into an isolated workspace dir
(`<bindir>/tests/luacats/stub/`) with a `.luarc.json` copied beside it: a `---@meta`
file *defines* but never *uses* its types, so the type/annotation diagnostics that
matter (`undefined-doc-name`/`-class`, `unknown-operator`) are forced on via
`neededFileStatus: Any!` — which only fires in a *workspace* check, so it must be a
directory check with the config co-located (a single-file `--check` ignores it).
This lint is what caught the invalid `---@operator eq/lt/le/index` emissions.

## Test-side type gates (mypy)
Three test-side mypy gates:
- `stubcheck` — mypy over each stub tree.
- `typingcases` — pytest-mypy-testing cases in `tests/python/test_types.mypy-testing`
  against the backend-neutral canonical name `welder_test` on `MYPYPATH`. Covers the
  scalar surfaces AND the STL-container typing (stl.hpp below): a container
  *parameter* types as the wide accepted input (`Sequence[T]` — lists and tuples
  typecheck), a *return* as the concrete `list[T]`/`dict[K, V]`/`tuple[…]`/`T | None`;
  the `# R:` reveals must match BOTH rods' stubs byte-for-byte (mypy 2.x concise
  names: `list[int]`, `int | None`, classes fully qualified `welder_test.stl.Item`),
  which also means only *returns* are revealed — parameter spellings differ per
  stubgen (pybind11 `typing.SupportsInt` vs nanobind `int`), so argument typing is
  asserted by *acceptance* (a call that must typecheck), never revealed.
- `mypy.tests` — plain mypy over the `.py` specs (which are `Any` to mypy via the
  `ModuleType` fixture).

## Test layout & harness
Tests split by target language under `tests/`: the backend-neutral C++ case tree
(`common/cpp/`) and compile-only core checks (`core/`) live at the top; the Python
rods + their pytest specs under `python/`, the Lua rod + its busted specs
under `lua/`. The LuaCATS stub rod has its own top-level tree `tests/luacats/`
(compile + run + byte-exact golden + optional lint, needing only the compiler to
build — it depends on neither sol2 nor Lua): `welder_luacats_generate_stub` builds
`cpp/stub_gen.cpp` (a *dedicated doc-rich case*, since the shared cases are
behavior-focused and mostly carry no `doc` — same reason `doc.hpp` is its own Python
case) into `<bindir>/tests/luacats/stub/stubdemo.lua`, the `luacats.stub_golden`
CTest compares it to `stub.golden.lua`, and — when a `lua-language-server` is found —
`stubcheck.luacats` lints it (see the LuaCATS stubcheck note above). The **C++ case tree is shared across all three rods**
(`common/cpp/`, welded for `lang::py` **and** `lang::lua`), reached only through
three macros each `bindings.cpp` defines — `WELDER_TEST_WELDER` (the
`welder::welder<Rod>` entry-point instantiation, e.g.
`::welder::welder<::welder::rods::pybind11::rod>`), `WELDER_TEST_MODULE_T` (module
handle type) and `WELDER_TEST_SUBMODULE` (the one module-handle op the `register_*`
helpers need: `def_submodule` for Python, a nested-table helper for sol2).
`WELDER_TEST_MULTIPLE_INHERITANCE` gates the diamond case (pybind11 + sol2 —
nanobind is single-inheritance, and the Python spec skips the diamond when `Bottom`
is absent). `WELDER_TEST_STYLED_WELDER` is a fourth seam, used only by `naming.hpp`:
`welder::welder<Rod, Style>` with the backend's chosen name style (the runtime
naming test; see the feature list below). Only the genuinely rod-specific case files
(`trust.hpp` hand-registration, `caster.hpp` type_caster) are per-rod, under
`tests/python/{pybind11,nanobind}/cpp/`.

**Python trees** (`tests/python/`): uv + pytest + CTest; welder is header-only, so
each backend builds a single extension (consuming `<welder/vocabulary.hpp>`). The
behavioral specs (`test_*.py` + `conftest.py`) are backend-agnostic and select the
module at runtime via `WELDER_TEST_MODULE`, so the same specs run against pybind11
*and* nanobind. The uv env is still prepared by the top `tests/CMakeLists.txt` (the
doxyfilter tests also use it for `lark`) but manages the pyproject that now lives in
`python/`.

**Lua tree** (`tests/lua/`): **busted** (installed via luarocks, nothing vendored —
see the sol2 section above) is the Lua counterpart of uv+pytest. The shared
`common/cpp` groups (all but the two Python-only ones: `doc.hpp`, whose
`__doc__`/`attr` hooks have no Lua slot, and `stl.hpp`, whose audience is mypy)
build into a `.so` per enabled Lua rod — `welder_test_sol2.so`
(`cpp/bindings.cpp`) and `welder_test_luabridge.so` (`cpp/bindings_luabridge.cpp`) —
and the **same** per-group busted specs (`spec/*_spec.lua`) assert **both**: the spec
`helper.lua` picks the module by the `WELDER_TEST_LUA_MODULE` env var (set per CTest
`luatest.sol2` / `luatest.luabridge`), the Lua analogue of the Python specs'
`WELDER_TEST_MODULE`. A shared busted install is reused across rods that target the
same Lua minor; each rod also has a `negcompile.<rod>_unwelded` WILL_FAIL CTest.
Where a case carries per-language marks, both Lua rods see the same lua-resolved binding;
where a backend can't represent a feature (LuaBridge3's virtual diamond) the spec skips it.
Where a case carries per-language marks, Lua sees the lua-resolved binding, which
differs from Python by design. `LUA_PATH`/`LUA_CPATH` (set by the CMakeLists) reach
`helper.lua` (which `require`s the `.so` once) and the built module. CTests
`luatest.sol2` and `negcompile.sol2_unwelded` (WILL_FAIL: an unwelded userdata
member is rejected by the gate). NB the Lua-version-matching gotcha above.

nanobind extensions are built with nanobind's own `nanobind_add_module` (it
compiles the nanobind runtime in) and stubbed with its own **bundled** stubgen
(`nanobind_add_stub`, `RECURSIVE` → a package tree per variant; no extra pip
package — `stubgen.py` is stdlib-only on Python ≥3.11, run via the build's
`Python_EXECUTABLE`, which loads the extension by dynamic lookup), then
`stubcheck.*` runs mypy over it. The `typingcases.*` type-level gate
(`test_types.mypy-testing`, run via pytest-mypy-testing) now runs against **both**
rods: each copies its generated stub tree to the canonical name `welder_test`
on `MYPYPATH` and asserts the same revealed types — rename-safe because both stub
trees use only relative imports. (nanobind's copy hangs off its stub *target*'s
POST_BUILD, since its stubs are a separate custom target.) Key locations by feature:
- `tests/common/cpp/enums.hpp` + `tests/python/test_enums.py` — enums
- `tests/common/cpp/overloads.hpp` + `tests/python/test_overloads.py` + `tests/lua/spec/overloads_spec.lua` — per-overload / per-constructor marks (carriage-computed overload groups; opt_in-vs-constructibility)
- `tests/python/{pybind11,nanobind}/cpp/trust.hpp` + `tests/python/test_trust.py` — trust hatches
- `tests/python/{pybind11,nanobind}/cpp/caster.hpp` + `tests/python/test_caster.py` — self-contained type casters
- `tests/python/{pybind11,nanobind}/cpp/neg/` — bindability negative-compile (`negcompile.*` CTests, `WILL_FAIL`)
- `tests/core/template_annotations.cpp` — template↔annotation semantics (compile-only)
- `tests/core/naming.cpp` — name styling + `weld_as` resolution (`compile.naming`, compile-only static_asserts)
- `tests/common/cpp/naming.hpp` + `tests/python/test_naming.py` + `tests/lua/spec/naming_spec.lua` —
  the *runtime* naming coverage: a `styling` namespace of camelCase members bound
  through a **styled** welder (the extra seam `WELDER_TEST_STYLED_WELDER`, defined per
  backend beside `WELDER_TEST_WELDER` — PEP 8 for the Python rods, `naming::snake_case`
  for sol2), asserting the reshaped names and the per-language `weld_as` verbatim
  override. luacats keeps its own dedicated `stub_gen.cpp`, so its golden is unaffected.
- `tests/common/cpp/doc.hpp` `Gadget`/`combine` + `tests/python/test_doc.py` — docstrings (multiline/dedent)
- `tests/common/cpp/stl.hpp` + `tests/python/test_stl.py` + the container cases in
  `test_types.mypy-testing` — STL-container conversions (vector/map/optional/pair,
  free functions AND container-typed members): runtime round-trips plus the stub
  typing above. Python-only group (like doc.hpp — not included by the Lua rods);
  nanobind's bindings.cpp carries the per-container `<nanobind/stl/*.h>` includes
  it needs (map/optional/pair; pybind11's stl.h is all-in-one)
- `tests/doxyfilter/` — Doxygen filter goldens + e2e (run with the uv venv Python, pins `lark`)

Gotcha: uv rejects the Homebrew python for some operations — see the test-harness
memory note if you hit it.

## Windows: nanobind stable ABI (abi3) — the shippable / cross-compiler case
`-DWELDER_NANOBIND_STABLE_ABI=ON` builds the nanobind extension against Python's
**stable ABI (abi3)**: `nanobind_add_module(... STABLE_ABI ...)` +
`find_package(Python … Development.SABIModule)` → the `.pyd` links `python3.dll`
via `Py_LIMITED_API` instead of `pythonXY.dll`. Because the limited API is a **C
ABI**, the module loads across Python minors *and* across the GCC/MSVC compiler
boundary — so a **UCRT-MinGW gcc-16** build imports into a **stock MSVC CPython**
(python.org / Blender), not just the mingw python. This is the only path to a
shippable Windows binding today (welder is gcc-16-only; MSVC/Clang lack P2996), and
it is **nanobind-only** — pybind11 has no abi3 equivalent. Needs Python ≥ 3.12 and
CMake ≥ 3.26 (`Development.SABIModule`); below 3.12 nanobind silently downgrades.

On MinGW the option also (in `tests/python/nanobind/CMakeLists.txt`): statically
folds in the gcc runtimes (`-static -static-libgcc -static-libstdc++`) so the `.pyd`
is self-contained (no `libstdc++-6`/`libgcc_s`/`libwinpthread` on the host PATH), and
overrides nanobind's `-Os` on the binding TU with `-O2` — gcc-16.1 has an `-Os`
codegen bug that emits an unresolved out-of-line ref to `std::string`'s move ctor
(which `-O2` inlines away). CI job **`windows-abi3`** proves it end to end: compiles
welder's shared nanobind bindings with ucrt64 gcc against an `actions/setup-python`
MSVC CPython, asserts the `.pyd`'s only DLL deps are `python3.dll` + UCRT (no gcc
runtimes), imports it under that MSVC interpreter and runs the shared specs.
Origin/research: `skarndev/nb-mingw-abi3-poc`.
