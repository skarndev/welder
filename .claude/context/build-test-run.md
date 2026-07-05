# Build, run & test

Read when: building, running the examples, or working on tests / .pyi stubs.

## Build & run
```bash
conan install . -pr:a conan/profiles/gcc16 --build=missing
cmake --preset welder-gcc16 -DWELDER_LUA_DIR="$(brew --prefix lua@5.4)"
cmake --build --preset welder-gcc16
```

**Version knobs (provider-neutral):** conan supplies the C++ backend headers
(sol2/pybind11/nanobind), but the *language runtimes* come from the system/user via
`find_package`, so a consumer can bring their own without conan. `WELDER_LUA_VERSION`
(default `5.4`) + `WELDER_LUA_DIR` (a Lua install prefix) pick the Lua the sol2
backend builds against and its tests run with; `WELDER_PYTHON_VERSION` (default
`3.14`) feeds `find_package(Python …)` + `uv sync --python`. Point `CMAKE_PREFIX_PATH`
/ these vars at your own installs to override the conan defaults.

Both example Python modules are then importable:
```bash
PYTHONPATH=build/welder-gcc16/examples/python_poc \
  python3 -c "import welder_poc as w; p=w.Point(); p.x=1.5; print(p.x)"
PYTHONPATH=build/welder-gcc16/examples/python_poc_headeronly \
  python3 -c "import welder_poc_ho as w; print(hasattr(w.Label(), 'cache'))"  # False
```

The Lua example (`examples/lua_poc` → `shapes_lua.so`) is a `require`-able module
(needs a Lua interpreter with `package.cpath` pointing at its dir):
```lua
package.cpath = "build/welder-gcc16/examples/lua_poc/?.so"
local s = require("shapes_lua")
local r = s.Rect(3.0, 4.0); print(r:area())   -- 12.0
```

## Lua backend build/test (sol2)
`welder::sol2` needs conan `with_sol2` (for the `sol2` C++ headers). **Lua itself is
NOT from conan** — `src/welder/backends/CMakeLists.txt` finds it with CMake's builtin
`FindLua`, pinned to `WELDER_LUA_DIR` so the user's install (not conan's transitive
`lua`, which shadows the search via `CMAKE_PREFIX_PATH`) provides the headers. A Lua
extension is created with `welder_sol2_add_module(<name> <sources>)`
(cmake/WelderSol2Module.cmake): bare `<name>.so`, host-symbol link model, and
**`CXX_SCAN_FOR_MODULES OFF`** (sol2's `<luaconf.h>` fails p1689 module scanning — a
header-unit macro-visibility issue with `LLONG_MAX`; so a Lua TU is header-only,
never `import welder;`). Gated by `WELDER_BUILD_SOL2` (default ON).

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

## `.pyi` stub generation
Via [pybind11-stubgen](https://github.com/pybind/pybind11-stubgen) (build-time):
`cmake/WelderPybind11Stubgen.cmake` → `welder_pybind11_generate_stubs(<target>
PYTHON <interp> …)`, a POST_BUILD step (`--exit-code`); gated by `WELDER_BUILD_STUBS`
(default ON). `PYTHON` must import the extension (ABI match) and have stubgen;
welder docstrings flow into the stubs. pybind11-stubgen is pinned to its GitHub
`main` branch (fixes not yet on PyPI; see `tests/pyproject.toml`
`[tool.uv.sources]`). Examples opt in via `-DWELDER_STUBGEN_PYTHON=<interp>`.

## Test-side type gates (mypy)
Three test-side mypy gates:
- `stubcheck` — mypy over each stub tree.
- `typingcases` — pytest-mypy-testing cases in `tests/python/test_types.mypy-testing`
  against the backend-neutral canonical name `welder_test` on `MYPYPATH`.
- `mypy.tests` — plain mypy over the `.py` specs (which are `Any` to mypy via the
  `ModuleType` fixture).

## Test layout & harness
Tests split by target language under `tests/`: the backend-neutral C++ case tree
(`common/cpp/`) and compile-only core checks (`core/`) live at the top; the Python
backends + their pytest specs under `python/`, the Lua backend + its busted specs
under `lua/`. The **C++ case tree is shared across all three backends**
(`common/cpp/`, welded for `lang::py` **and** `lang::lua`), reached only through
three macros each `bindings.cpp` defines — `WELDER_TEST_BE` (backend namespace),
`WELDER_TEST_MODULE_T` (module handle type) and `WELDER_TEST_SUBMODULE` (the one
module-handle op the `register_*` helpers need: `def_submodule` for Python, a
nested-table helper for sol2). `WELDER_TEST_MULTIPLE_INHERITANCE` gates the diamond
case (pybind11 + sol2 — nanobind is single-inheritance, and the Python spec skips the
diamond when `Bottom` is absent). Only the genuinely backend-specific case files
(`trust.hpp` hand-registration, `caster.hpp` type_caster) are per-backend, under
`tests/python/{pybind11,nanobind}/cpp/`.

**Python trees** (`tests/python/`): uv + pytest + CTest; each extension is
dual-variant (module + header-only). The behavioral specs (`test_*.py` +
`conftest.py`) are backend-agnostic and select the module at runtime via
`WELDER_TEST_MODULE`, so the same specs run against pybind11 *and* nanobind. The uv
env is still prepared by the top `tests/CMakeLists.txt` (the doxyfilter tests also
use it for `lark`) but manages the pyproject that now lives in `python/`.

**Lua tree** (`tests/lua/`): **busted** (installed via luarocks, nothing vendored —
see the sol2 section above) is the Lua counterpart of uv+pytest. The shared
`common/cpp` groups (all but `doc.hpp`, whose `__doc__`/`attr` hooks are
Python-only) build into `welder_test_sol2.so`; per-group busted specs
(`spec/*_spec.lua`, mirroring the `.py` files) assert them in one `busted` run.
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
backends: each copies its module-form stub tree to the canonical name `welder_test`
on `MYPYPATH` and asserts the same revealed types — rename-safe because both stub
trees use only relative imports. (nanobind's copy hangs off its stub *target*'s
POST_BUILD, since its stubs are a separate custom target.) Key locations by feature:
- `tests/common/cpp/enums.hpp` + `tests/python/test_enums.py` — enums
- `tests/python/{pybind11,nanobind}/cpp/trust.hpp` + `tests/python/test_trust.py` — trust hatches
- `tests/python/{pybind11,nanobind}/cpp/caster.hpp` + `tests/python/test_caster.py` — self-contained type casters
- `tests/python/{pybind11,nanobind}/cpp/neg/` — bindability negative-compile (`negcompile.*` CTests, `WILL_FAIL`)
- `tests/core/template_annotations.cpp` — template↔annotation semantics (compile-only)
- `tests/common/cpp/doc.hpp` `Gadget`/`combine` + `tests/python/test_doc.py` — docstrings (multiline/dedent)
- `tests/doxyfilter/` — Doxygen filter goldens + e2e (run with the uv venv Python, pins `lark`)

Gotcha: uv rejects the Homebrew python for some operations — see the test-harness
memory note if you hit it.
