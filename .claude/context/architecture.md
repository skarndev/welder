# Architecture

Read when: planning or changing architecture, adding a backend/language, or you
need the file/dir map or the backend-interface contract.

Language-agnostic **core** + pluggable **backends**, joined by **static
polymorphism**. The core owns *all* the reflection work — deciding **what** binds
(`bind_traits.hpp`), whether each type is representable (`bindable.hpp`), and
walking types/namespaces/bases to drive a binding (`backend.hpp`'s generic
driver). A **backend** is a stateless policy struct satisfying the `welder::backend`
concept: it supplies only the *emission primitives* (how to register a class /
method / property / module attribute in its framework) and never re-implements
the traversal or annotation semantics. Adding a language = writing one backend
struct + thin public wrappers; the core is reused verbatim. The core never depends
on a backend.

`src/` is the include root, so every public include starts from `welder/`.

## File / directory map

```
src/welder/
  detail/config.hpp     WELDER_EXPORT macro (export under the module, else empty)
  lang.hpp              enum class lang                       — std-free vocabulary
  annotations.hpp       weld / policy / mark / doc + mask helpers — std-free vocabulary
  reflect.hpp           welded_for / policy_of / member_bound / trusted_for / public_bases — uses <meta>
  doc.hpp               doc_of / return_doc_of / param_docs / doc styles / function_docstring — uses <meta>
  bind_traits.hpp       backend-agnostic "what binds": param/ctor/method/operator/namespace-member selectors + native-base collection — uses <meta>
  bindable.hpp          caster_oracle concept + generic bindability gate (STL-wrapper recursion) — uses <meta>
  backend.hpp           the `welder::backend` concept (emission contract) + generic driver (bind_type / bind_namespace_driver / build_module_driver)
  module.hpp            WELDER_MODULE(ns, backend) entry-point dispatch macro
  welder.hpp            header-only umbrella: lang+annotations+reflect+doc
  welder.cppm           the single `export module welder;` (exports vocabulary only)
  backends/
    pybind11.hpp        pybind11 backend: struct detail::backend (emission primitives) + public bind<T> / bind_namespace / build_module wrappers over the driver
    CMakeLists.txt      target: welder::pybind11  (nanobind / lua planned here)
src/CMakeLists.txt      targets: welder::headers / welder::module
cmake/
  WelderPybind11Stubgen.cmake  welder_pybind11_generate_stubs() — .pyi via pybind11-stubgen
tools/
  welder_doxygen_filter.py     Doxygen INPUT_FILTER driver: welder annotations → Doxygen comments (needs `lark`)
  welder_doxygen_filter.lark   its grammar: C++ lexical soup (layer 1) + attribute-list (layer 2)
examples/
  python_poc/             consumes `import welder;`
  python_poc_headeronly/  consumes welder header-only
  welder_module/          whole-module binding via WELDER_MODULE
docs/                     the documentation site (gated by WELDER_BUILD_DOCS, OFF)
  CMakeLists.txt          targets welder-docs / welder-docs-serve; provisions the uv env, fetches doxygen-awesome, patches the Doxygen header
  mkdocs.yml              mkdocs-material config (docs_dir: content)
  content/                the narrative guide (index, guide/*, architecture, reference) + stylesheets/extra.css
  Doxyfile.in             configured → Doxygen C++ reference (src/welder/** via the INPUT_FILTER) into content/api/ (gitignored; mkdocs copies to site/api)
  api_mainpage.md         the Doxygen landing page (USE_MDFILE_AS_MAINPAGE)
  doxygen-extra.css       spark-palette retune over doxygen-awesome-css
  patch_doxygen_header.py injects the doxygen-awesome dark-mode/extension JS into the generated header
  pyproject.toml/uv.lock  isolated docs env: mkdocs-material + lark
```

## Layering rules

`bind_traits.hpp`, `bindable.hpp` and `backend.hpp` are part of the reflection
layer (like `reflect.hpp`/`doc.hpp`): header-only, `<meta>`-using, **not** part of
the `welder` module, and they do **not** include `annotations.hpp` (the vocabulary
arrives first via `import welder;` or `welder.hpp`). `doc.hpp` follows the same
rule. `module.hpp` is macro-only and backend-agnostic; each backend header defines
its `WELDER_DETAIL_MODULE_ENTRY_<backend>` expansion.

See `gcc16-toolchain.md` for the module-vs-header boundary that forces this
layering (a gcc-16 std-in-purview leak).

## The backend interface (static polymorphism)

A backend is a stateless struct `B` satisfying `welder::backend` (`backend.hpp`).
The core's generic driver (`welder::detail::bind_type` / `bind_namespace_driver` /
`build_module_driver`) is templated on `B` and calls its members; the public
`welder::pybind11::bind` / `bind_namespace` / `build_module` are one-line wrappers
that plug in `pybind11::detail::backend`. `B` provides:

- **Associated:** `static constexpr lang language;` the target language;
  `using module_type = …;` the module handle; `template<class T> static constexpr
  bool has_native_caster;` — the `caster_oracle` leaf: is `T` convertible *without*
  welder registering a class for it? (false ⇒ welder requires `T` welded). This is
  the one bindability fact the core can't know; the STL-wrapper recursion in
  `bindable.hpp` is shared.
- **Type binding:** `make_class<T, Bases…>`, `add_default_ctor`, `add_constructor<Ctor>`,
  `add_aggregate_constructor<T>`, `add_field<Mem>`, `add_method<Fn>`,
  `add_static_method<Fn>`, `add_operator<Fn>`, and `consteval special_method_name(op)`
  (the operator→target-name map, e.g. pybind's `operator+`→`__add__`; nullptr =
  not exposed, which also gates operator eligibility in the driver).
- **Enum binding:** `make_enum<E>`, `add_enumerator<Enum>`, `finish_enum<E>` (the
  whole-enum finalizer, e.g. pybind's `export_values()` for unscoped enums).
- **Namespace/module binding:** `open_module`→ a per-(sub)module *session* (backend
  scratch state — pybind uses it to batch live variable properties), `set_module_doc`,
  `add_function<Fn>`, `add_variable<Var>` (const→snapshot, else a live property),
  `add_submodule`, `close_module` (finalize the session).

The concept statically checks the associated types and the module machinery; the
class/per-member hooks are templated on a reflection, so they are
contract-by-documentation, enforced when the driver instantiates. A nanobind
backend is nearly a copy of the pybind11 one (same class-handle model); a Lua
backend implements the same ~16 primitives against Lua's C API.

**Backend namespace.** The pybind11 backend is `welder::pybind11` (nanobind →
`welder::nanobind`; both target `lang::py`). Inside it, unqualified `pybind11`
would resolve to that namespace, so the header aliases `namespace py = ::pybind11;`
once and uses `py::` throughout.

Complex/custom type conversions are intended to be registered per-backend via
pybind11's own mechanisms, separately from core resolution — design pending.

## CMake targets

- **`welder::headers`** — INTERFACE, the header-only core (include path + flags).
- **`welder::module`** — STATIC, builds `src/welder/welder.cppm`; provides `import welder;`.
- **`welder::pybind11`** — INTERFACE, the pybind11 backend (links headers + pybind11 + Python).
  Gated by `WELDER_BUILD_PYBIND11`. Future Python (nanobind) / Lua backends get
  their own `welder::<backend>` target alongside it.

Reflection/module flags are isolated in the `welder_flags` INTERFACE target and
gated on compiler id, so nothing gcc-specific leaks into the public targets.
