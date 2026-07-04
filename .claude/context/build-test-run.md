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
The harness is uv + pytest + CTest; each bindings extension is dual-variant (module
+ header-only). The behavioral specs (`tests/test_*.py` + `conftest.py`) are
**backend-agnostic** and shared: each backend tree builds its extension(s) and
registers CTest entries that select the module at runtime via `WELDER_TEST_MODULE`,
so the same specs run against pybind11 *and* nanobind (a cross-backend consistency
check). The **C++ case tree is shared too**: the backend-neutral case headers live
in `tests/common/cpp/` and reach the backend only through two macros each
`bindings.cpp` defines — `WELDER_TEST_BE` (backend namespace) and
`WELDER_TEST_MODULE_T` (module handle type); `WELDER_TEST_MULTIPLE_INHERITANCE`
gates the diamond case (pybind11 only — nanobind is single-inheritance, and the
Python spec skips the diamond when `Bottom` is absent). Only the genuinely
backend-specific case files (`trust.hpp` hand-registration, `caster.hpp`
type_caster) are per-backend, under `tests/pybind11/cpp/` and `tests/nanobind/cpp/`.

nanobind extensions are built with nanobind's own `nanobind_add_module` (it
compiles the nanobind runtime in) and stubbed with its own **bundled** stubgen
(`nanobind_add_stub`, `RECURSIVE` → a package tree per variant; no extra pip
package — `stubgen.py` is stdlib-only on Python ≥3.11, run via the build's
`Python_EXECUTABLE`, which loads the extension by dynamic lookup), then
`stubcheck.*` runs mypy over it. The `typingcases` type-level gate stays pybind11
-only (it exposes module-form stubs under the canonical `welder_test` name). Key
locations by feature:
- `tests/common/cpp/enums.hpp` + `test_enums.py` — enums
- `tests/{pybind11,nanobind}/cpp/trust.hpp` + `test_trust.py` — trust hatches
- `tests/{pybind11,nanobind}/cpp/caster.hpp` + `test_caster.py` — self-contained type casters
- `tests/{pybind11,nanobind}/cpp/neg/` — bindability negative-compile (`negcompile.*` CTests, `WILL_FAIL`)
- `tests/core/template_annotations.cpp` — template↔annotation semantics (compile-only)
- `tests/common/cpp/doc.hpp` `Gadget`/`combine` + `test_doc.py` — docstrings (multiline/dedent)
- `tests/doxyfilter/` — Doxygen filter goldens + e2e (run with the uv venv Python, pins `lark`)

Gotcha: uv rejects the Homebrew python for some operations — see the test-harness
memory note if you hit it.
