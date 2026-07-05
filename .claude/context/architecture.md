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
    python/
      doc_style.hpp         Python docstring styles shared by both backends — welder::python::google_style (moved out of doc.hpp, which keeps only the neutral doc_style concept + function_docstring)
      operators.hpp         C++-operator → Python dunder map shared by both backends — welder::python::operator_dunder (identical across pybind11/nanobind; mirrors doc_style.hpp)
      pybind11/backend.hpp  pybind11 backend: struct detail::backend (public emission primitives + protected _ helpers) + public bind<T> / bind_namespace / build_module wrappers over the driver
      nanobind/backend.hpp  nanobind backend: the same, against nanobind's API (def_rw/def_ro, nb::init, placement-__init__, is_base_caster gate, NB_MODULE)
    lua/
      overloads.hpp         Overload-set selectors shared by BOTH Lua backends — welder::lua::{method,operator,function}_overload_set / is_overload_leader / overload_group; both gather a name's overloads (sol2 → sol::overload, luacats → ---@overload) because the driver visits overloads one at a time. Mirrors backends/python/doc_style.hpp.
      sol2/backend.hpp      sol2 Lua backend: struct detail::backend (emission primitives + protected _ helpers: constructor-set gathering, welded-base closure, overload registration), against sol2's API (module_type = sol::table; usertype/new_usertype; enums as name→value tables; luaopen_ entry macro)
      sol2/metamethods.hpp  C++-operator → sol2/Lua metamethod map (welder::sol2::detail::operator_mm) split out of the sol2 backend — the sol2 analogue of backends/python/operators.hpp (Lua's asymmetric set: no __ne/__gt/__ge; ^→__bxor; 5.3-gated bitwise)
      luacats/backend.hpp   LuaCATS `---@meta` stub backend: text-emitting welder::backend over the SAME driver (no sol2/Lua dep); struct detail::backend wires the type map + document assembler to the driver; ---@overload grouping; WELDER_LUACATS_MAIN generator entry
      luacats/type_map.hpp  the LuaCATS rendering primitives: C++→LuaCATS type map (lua_type_string), ---@operator name map (operator_luacats), the is_native_lua caster trait, and the --- comment text helpers
      luacats/document.hpp  the LuaCATS document assembler: signature/overload rendering + the RAII *_writer handle types (document / module_writer / class_writer / enum_writer) the driver's module/class/enum handles deduce to
    CMakeLists.txt      targets: welder::pybind11, welder::nanobind, welder::sol2, welder::luacats
src/CMakeLists.txt      targets: welder::headers / welder::module
cmake/
  WelderPybind11Stubgen.cmake  welder_pybind11_generate_stubs() — .pyi via pybind11-stubgen
  WelderSol2Module.cmake       welder_sol2_add_module() — build a loadable Lua .so (bare name, host-symbol link model, module-scan OFF)
  WelderLuaCATSStub.cmake      welder_luacats_generate_stub() — build a generator exe (welder::luacats) + run it → <name>.lua (ALL target)
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
that plug in `pybind11::detail::backend`.

**Struct convention (all backends).** Each `detail::backend` reads as one unit: the
associated types up top, then a `protected:` block of framework-specific
implementation helpers (each prefixed `_`, e.g. `_needs_registration`,
`_def_function`, `_make_usertype`), then the `public:` emission primitives — the
`welder::backend` contract — written in terms of them. A
`static_assert(welder::backend<backend>)` immediately follows the struct so a
contract mismatch is a local, named error rather than a deep instantiation failure.
Cross-backend maps that would otherwise be duplicated verbatim are factored into
shared headers instead of the struct: the Python dunder map into
`backends/python/operators.hpp`, the sol2 metamethod map into
`backends/lua/sol2/metamethods.hpp`, and the LuaCATS type map + document assembler
into `backends/lua/luacats/{type_map,document}.hpp`.

`B` provides:

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
contract-by-documentation, enforced when the driver instantiates. The nanobind
backend is nearly a copy of the pybind11 one (same class-handle model), diverging
only where nanobind's API does — `def_rw`/`def_ro`, `nb::init`, a
placement-`__init__` aggregate factory, module docstrings via `__doc__`, the
`is_base_caster` bindability probe, and `is_arithmetic` enums (Python `IntEnum`, to
match pybind11's int-convertible enums). Its one gap is multiple inheritance:
nanobind binds a single base per class, so a multi-base diamond binds under
pybind11 but not nanobind.

The **sol2 (Lua)** backend implements the same ~16 primitives against sol2, and is
where the emission contract stretches furthest from Python — the divergences are
the interesting part:
- **`module_type = sol::table`** (a Lua module is a table); the borrowed
  `lua_State*` is viewed with `sol::state_view`. `open_module`/`close_module` are
  no-ops (empty session) — namespace variables bind eagerly as snapshots.
- **caster oracle** reads `sol::lua_type_of<T>` (a `userdata` classification ⇒
  needs registration); enums are forced needs-registration so a welded enum's
  name→value table is required, matching the Python backends.
- **operators → Lua metamethods**, a *smaller, asymmetric* map (`special_method_name`
  returns the `__name`, `add_operator` the `sol::meta_function`): no `__ne`/`__gt`/
  `__ge` (Lua derives `~=`/`>`/`>=` from `__eq`/`__lt`/`__le`), `^`→`__bxor` not
  `__pow`, and bitwise metamethods `#if`-gated to Lua ≥ 5.3.
- **constructors registered up front** in `make_class` from reflection
  (`sol::constructors<…>` built via `substitute`), because sol2 wants the whole set
  at once — so `add_default_ctor`/`add_constructor`/`add_aggregate_constructor` are
  no-ops. Aggregates ride C++26 parenthesized aggregate init.
- **full welded-base closure**: sol2's `sol::bases<…>` must list *every* welded
  ancestor (it doesn't chain through an intermediate usertype's bases), so the
  backend recomputes the transitive set itself rather than using the core's
  *nearest*-ancestor `native_base_types`. It supports multiple + virtual bases (the
  diamond binds, unlike nanobind).
- **enums are name→value tables** (Lua has no enum type); an unscoped enum's names
  are also mirrored onto the enclosing module.
- **no runtime docstrings** (`doc`/`returns` ignored — their home is the
  `welder::luacats` LuaCATS stub backend, below). **Overloaded methods, static
  methods, free functions and operators are grouped into one `sol::overload(…)`**:
  sol2 stores one value per name/metamethod slot, so the backend gathers a name's
  overloads (mirroring how it already gathers a type's constructors "all at once")
  rather than letting the last registered win. Grouping happens in the sol2 backend
  (`method_overloads`/`operator_overloads`/`function_overloads` re-invoke the core
  selection predicates); the driver still visits overloads one at a time, which suits
  pybind11's incremental `.def`.
- entry point: `WELDER_MODULE(ns, sol2)` emits `extern "C" luaopen_<ns>` returning
  the module table.

**Backend namespace.** The pybind11 backend is `welder::pybind11`, the nanobind one
`welder::nanobind` (both target `lang::py`); the sol2 backend is `welder::sol2`
(target `lang::lua`). Inside the Python backends, unqualified `pybind11` /
`nanobind` would resolve to that namespace, so each aliases `namespace py =
::pybind11;` / `namespace nb = ::nanobind;` and uses `py::` / `nb::` throughout.
`welder::sol2` does *not* shadow the library's `::sol` namespace, so it uses `sol::`
directly (no alias). The **`welder::luacats`** stub backend also targets `lang::lua`
(it emits the Lua *type stub*, so it reflects the same `weld(…, lang::lua)` types),
but is a *build-time text emitter*, not a runtime binding — see below and
`docs-and-doxygen.md`.

Complex/custom type conversions are intended to be registered per-backend via each
framework's own mechanisms, separately from core resolution — design pending.

## CMake targets

- **`welder::headers`** — INTERFACE, the header-only core (include path + flags).
- **`welder::module`** — STATIC, builds `src/welder/welder.cppm`; provides `import welder;`.
- **`welder::pybind11`** — INTERFACE, the pybind11 backend (links headers + pybind11 + Python).
  Gated by `WELDER_BUILD_PYBIND11`.
- **`welder::nanobind`** — INTERFACE, the nanobind backend. nanobind compiles its
  runtime *into* each extension via its own `nanobind_add_module()`, so this target
  only surfaces the welder + nanobind headers; an extension is still created with
  `nanobind_add_module`. Gated by `WELDER_BUILD_NANOBIND`.
- **`welder::sol2`** — INTERFACE, the sol2 Lua backend. A loadable Lua C module must
  resolve `lua_*` from the *host* interpreter and not bundle its own runtime, so this
  target surfaces only the sol2 + Lua *headers* (it does not link `lua::lua`). Create
  an extension with `welder_sol2_add_module()` (cmake/WelderSol2Module.cmake), which
  sets the bare `<name>.so`, the host-symbol link model (`-undefined dynamic_lookup`
  on macOS), and `CXX_SCAN_FOR_MODULES OFF` (sol2's `<luaconf.h>` can't see
  `LLONG_MAX` under p1689 module scanning — a header-unit macro-visibility issue, so
  a Lua binding TU is header-only, never `import welder;`). Gated by
  `WELDER_BUILD_SOL2`; needs `sol2` + `lua` (conan `with_sol2`).
- **`welder::luacats`** — INTERFACE, the LuaCATS `---@meta` stub backend. Unlike the
  runtime backends it depends on neither a framework nor a language runtime (pure
  reflection → text), so it is **unconditional** — just `welder::headers`. Emit a
  stub with `welder_luacats_generate_stub()` (cmake/WelderLuaCATSStub.cmake), which
  builds a `WELDER_LUACATS_MAIN` generator executable and runs it into `<name>.lua`
  as an ALL target (`CXX_SCAN_FOR_MODULES OFF`, matching the Lua-side TUs).

Reflection/module flags are isolated in the `welder_flags` INTERFACE target and
gated on compiler id, so nothing gcc-specific leaks into the public targets.
