# Build, run & test

Read when: building, running the examples, or working on tests / .pyi stubs.

## Build & run
```bash
conan install . -pr:a conan/profiles/gcc16 --build=missing
cmake --preset welder-gcc16
cmake --build --preset welder-gcc16
```

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
`welder::sol2` needs conan `with_sol2` (pulls `sol2` + its transitive `lua`). A Lua
extension is created with `welder_sol2_add_module(<name> <sources>)`
(cmake/WelderSol2Module.cmake): bare `<name>.so`, host-symbol link model, and
**`CXX_SCAN_FOR_MODULES OFF`** (sol2's `<luaconf.h>` fails p1689 module scanning — a
header-unit macro-visibility issue with `LLONG_MAX`; so a Lua TU is header-only,
never `import welder;`). Gated by `WELDER_BUILD_SOL2` (default ON).

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
- `typingcases` — pytest-mypy-testing cases in `tests/test_types.mypy-testing`
  against the backend-neutral canonical name `welder_test` on `MYPYPATH`.
- `mypy.tests` — plain mypy over the `.py` specs (which are `Any` to mypy via the
  `ModuleType` fixture).

## Test layout & harness
The harness is uv + pytest + CTest; each Python bindings extension is dual-variant
(module + header-only). The behavioral specs (`tests/test_*.py` + `conftest.py`) are
**backend-agnostic** and shared: each Python backend tree builds its extension(s)
and registers CTest entries that select the module at runtime via
`WELDER_TEST_MODULE`, so the same specs run against pybind11 *and* nanobind (a
cross-backend consistency check). The **C++ case tree is shared across all three
backends** (`tests/common/cpp/`, welded for `lang::py` **and** `lang::lua`), reached
only through three macros each `bindings.cpp` defines — `WELDER_TEST_BE` (backend
namespace), `WELDER_TEST_MODULE_T` (module handle type) and `WELDER_TEST_SUBMODULE`
(the one module-handle op the `register_*` helpers need: `def_submodule` for Python,
a nested-table helper for sol2). `WELDER_TEST_MULTIPLE_INHERITANCE` gates the diamond
case (pybind11 + sol2 — nanobind is single-inheritance, and the Python spec skips the
diamond when `Bottom` is absent). Only the genuinely backend-specific case files
(`trust.hpp` hand-registration, `caster.hpp` type_caster) are per-backend, under
`tests/pybind11/cpp/` and `tests/nanobind/cpp/`.

**sol2 (Lua) tree** (`tests/sol2/`): no uv/pytest — the conan `lua` ships no
interpreter binary, so a small embedded-Lua host (`runner.cpp`, links `liblua`,
`-export_dynamic` so the loaded module resolves `lua_*` back) drives Lua assertions.
It reuses the shared `common/cpp` groups (all but `doc.hpp`, whose `__doc__`/`attr`
hooks are Python-only) built into `welder_test_sol2.so`, and asserts them in
`test.lua` (the Lua counterpart of the `.py` specs — where a case carries per-language
marks, Lua sees the lua-resolved binding, which differs from Python by design). CTests
`luatest.sol2` (runner + script) and `negcompile.sol2_unwelded` (WILL_FAIL: an
unwelded userdata member is rejected by the gate).

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
- `tests/common/cpp/enums.hpp` + `test_enums.py` — enums
- `tests/{pybind11,nanobind}/cpp/trust.hpp` + `test_trust.py` — trust hatches
- `tests/{pybind11,nanobind}/cpp/caster.hpp` + `test_caster.py` — self-contained type casters
- `tests/{pybind11,nanobind}/cpp/neg/` — bindability negative-compile (`negcompile.*` CTests, `WILL_FAIL`)
- `tests/core/template_annotations.cpp` — template↔annotation semantics (compile-only)
- `tests/common/cpp/doc.hpp` `Gadget`/`combine` + `test_doc.py` — docstrings (multiline/dedent)
- `tests/doxyfilter/` — Doxygen filter goldens + e2e (run with the uv venv Python, pins `lark`)

Gotcha: uv rejects the Homebrew python for some operations — see the test-harness
memory note if you hit it.
