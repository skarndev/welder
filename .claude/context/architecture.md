# Architecture

Read when: planning or changing architecture, adding a rod (backend)/language, or
you need the file/dir map or the rod-interface contract.

Language-agnostic **core** + pluggable **rods** (a rod = a welding rod, the backend
that lays a framework's bindings down), joined by **static polymorphism**. The core
owns *all* the reflection work — deciding **what** binds (`bind_traits.hpp`),
whether each type is representable (`bindable.hpp`), and walking
types/namespaces/bases to drive a binding (`welder.hpp`'s generic driver). A **rod**
(`welder::rods::<name>::rod`) is a stateless policy struct satisfying the
`welder::rod` concept: it supplies only the *emission primitives* (how to register
a class / method / property / module attribute in its framework) and never
re-implements the traversal or annotation semantics. The one public entry point,
`welder::welder<Rod>` (also in `welder.hpp`), is templated on the rod and shared by
all of them. Adding a language = writing one rod struct; the core and the entry
point are reused verbatim. The core never depends on a rod.

`src/` is the include root, so every public include starts from `welder/`.

## File / directory map

```
src/welder/
  detail/config.hpp     WELDER_EXPORT macro (export under the module, else empty)
  lang.hpp              enum class lang                       — std-free vocabulary
  annotations.hpp       weld / policy / mark / doc + mask helpers — std-free vocabulary
  reflect.hpp           welded_for / policy_of / member_bound / trusted_for / public_bases — uses <meta>
  doc.hpp               doc_of / return_doc_of / param_docs / doc styles / function_docstring — uses <meta>
  naming.hpp            name styling: split_words/join_words/restyle + naming::{none,uniform<Kind>,snake_case,…} + the name_style concept (per-kind transform_* hooks) + weld_as_of / name_of (weld_as override → else style hook) — uses <meta>, depends on vocabulary (weld_as_spec), NOT annotations.hpp
  bind_traits.hpp       rod-agnostic "what binds": param/ctor/method/operator/namespace-member selectors + native-base collection — uses <meta>
  bindable.hpp          caster_oracle concept + generic bindability gate (STL-wrapper recursion) — uses <meta>
  welder.hpp            the `welder::rod` concept (emission contract) + the **carriage** (`detail::basic_carriage<Resolution>`): the reflection-driven traversal driver, a stateless struct of static member templates (bind_type / bind_enum / bind_function / bind_variable / bind_namespace / bind_namespace_as_submodule / build_module + private bind_members) parameterized on a **resolution** policy (marker_resolution = stitch, greedy_resolution = tack). Aliases: `welder::stitch_welding_carriage` (default), `welder::tack_welding_carriage`, `welder::carriage` (= stitch). + the `welder::welder<Rod, Style=naming::none, Carriage=carriage>` public entry point (weld_type / weld_function / weld_variable / weld_namespace / weld_namespace_as_submodule / weld_module), each a one-line forward to the carriage (subclass it, or inject a carriage, to extend). Threads Style through the carriage → every generated name goes through name_of (call-site name override, else weld_as, else the style hook)
  module.hpp            WELDER_MODULE(ns, rod) entry-point dispatch macro
  vocabulary.hpp        header-only vocabulary form: lang+annotations only (exactly what the module exports)
  welder.cppm           the single `export module welder;` (exports vocabulary only)
  rods/
    python/
      doc_style.hpp         Python docstring styles shared by both Python rods — welder::rods::python::google_style (moved out of doc.hpp, which keeps only the neutral doc_style concept + function_docstring)
      naming.hpp            the PEP 8 name style shared by both Python rods — welder::rods::python::pep8 (inherits naming::snake_case, overrides class/enum → PascalCase, enumerators verbatim); mirrors doc_style.hpp/operators.hpp. The core naming machinery lives in <welder/naming.hpp>
      operators.hpp         C++-operator → Python dunder map shared by both Python rods — welder::rods::python::operator_dunder (identical across pybind11/nanobind; mirrors doc_style.hpp)
      pybind11/rod.hpp      pybind11 rod: struct welder::rods::pybind11::rod (public emission primitives + protected _ helpers), directly in its namespace beside `namespace py = ::pybind11`
      pybind11/module.hpp   the pybind11 WELDER_MODULE entry-point macro (WELDER_DETAIL_MODULE_ENTRY_pybind11); include only for full automation
      nanobind/rod.hpp      nanobind rod: the same, against nanobind's API (def_rw/def_ro, nb::init, placement-__init__, is_base_caster gate, NB_MODULE)
      nanobind/module.hpp   the nanobind WELDER_MODULE entry-point macro
    lua/
      overloads.hpp         Overload-set selectors shared by ALL Lua rods — welder::rods::lua::{method,operator,function}_overload_set / is_overload_leader / overload_group; all gather a name's overloads (sol2 → sol::overload, luabridge → variadic addFunction, luacats → ---@overload) because the driver visits overloads one at a time. Mirrors rods/python/doc_style.hpp.
      metamethods.hpp       C++-operator → Lua metamethod NAME map shared by BOTH runtime Lua rods — welder::rods::lua::lua_metamethod_name (the asymmetric set: no __ne/__gt/__ge; ^→__bxor; 5.3-gated bitwise). sol2 pairs each name with its sol::meta_function slot; luabridge registers by the name string directly.
      sol2/rod.hpp          sol2 Lua rod: struct welder::rods::sol2::rod (emission primitives + protected _ helpers: constructor-set gathering, welded-base closure, overload registration), against sol2's API (module_type = sol::table; usertype/new_usertype; enums as name→value tables)
      sol2/module.hpp       the sol2 WELDER_MODULE entry-point macro (emits luaopen_<ns>)
      sol2/metamethods.hpp  C++-operator → sol2 metamethod SLOT map (welder::rods::sol2::operator_mm); sources the __name from the shared rods/lua/metamethods.hpp and adds the sol::meta_function slot
      luabridge/rod.hpp     LuaBridge3 Lua rod: struct welder::rods::luabridge::rod (emission primitives + protected _ helpers), against LuaBridge3's move-based fluent registrar. module_type = a copyable {lua_State*, namespace-path} handle (re-open-by-path model); getGlobalNamespace/beginNamespace/beginClass/deriveClass; ctors via addConstructor + a `.new` factory static fn; enums as nested value namespaces; blanket Stack<Enum> spec. Includes <lua.hpp> before <LuaBridge/LuaBridge.h> itself.
      luabridge/module.hpp  the LuaBridge3 WELDER_MODULE entry-point macro (emits luaopen_<ns>; builds under _G[ns], returns the table + clears the _G binding)
      luacats/rod.hpp       LuaCATS `---@meta` stub rod: text-emitting welder::rod over the SAME driver (no sol2/Lua dep); struct welder::rods::luacats::rod wires the type map + document assembler to the driver; ---@overload grouping; the whole-stub generate<^^Ns>(os) static
      luacats/module.hpp    the WELDER_LUACATS_MAIN generator-main() macro; include only for a stub-generator TU
      luacats/type_map.hpp  the LuaCATS rendering primitives: C++→LuaCATS type map (lua_type_string), ---@operator name map (operator_luacats), the is_native_lua caster trait, and the --- comment text helpers
      luacats/document.hpp  the LuaCATS document assembler: signature/overload rendering + the RAII *_writer handle types (document / module_writer / class_writer / enum_writer) the driver's module/class/enum handles deduce to
    CMakeLists.txt      targets: welder::pybind11, welder::nanobind, welder::sol2, welder::luabridge, welder::luacats
src/CMakeLists.txt      targets: welder::headers / welder::module
cmake/
  WelderPybind11Stubgen.cmake  welder_pybind11_generate_stubs() — .pyi via pybind11-stubgen
  WelderSol2Module.cmake       welder_sol2_add_module() — build a loadable Lua .so (bare name, host-symbol link model, module-scan OFF)
  WelderLuaBridgeModule.cmake  welder_luabridge_add_module() — same, for the LuaBridge3 rod (links welder::luabridge)
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

`bind_traits.hpp`, `bindable.hpp` and `welder.hpp` are part of the reflection
layer (like `reflect.hpp`/`doc.hpp`): header-only, `<meta>`-using, **not** part of
the `welder` module, and they do **not** include `annotations.hpp` (the vocabulary
arrives first via `import welder;` or `vocabulary.hpp`). `doc.hpp` follows the same
rule. `module.hpp` is macro-only and rod-agnostic; each rod's `module.hpp` defines
its `WELDER_DETAIL_MODULE_ENTRY_<rod>` expansion.

See `gcc16-toolchain.md` for the module-vs-header boundary that forces this
layering (a gcc-16 std-in-purview leak).

## The rod interface (static polymorphism)

A rod is a stateless struct `B` satisfying `welder::rod` (`welder.hpp`), exposed as
`welder::rods::<name>::rod`. The core's traversal driver — the **carriage**
(`detail::basic_carriage<Resolution>`, a stateless struct of static member templates
`bind_type` / `bind_enum` / `bind_function` / `bind_variable` / `bind_namespace` /
`bind_namespace_as_submodule` / `build_module`, + private `bind_members`) — is templated
on `B` and calls its members; the public `welder::welder<B, Style, Carriage=carriage>`
(weld_type / weld_function / weld_variable / weld_namespace / weld_namespace_as_submodule
/ weld_module) is the shared, one-and-only entry point that plugs `B` into the carriage
— each entry point is a one-line forward to `Carriage::bind_*`, no per-rod wrappers.
`weld_function<^^fn>` / `weld_variable<^^var>` are the **semi-manual** route (bind one
free-standing function / global directly onto a module handle — the free-standing analogue
of `weld_type`, via the carriage's `bind_function` / `bind_variable`, which reuse the same
`add_function` / `add_variable` rod primitives the namespace walk uses per member,
`bind_variable` opening its own one-shot session around the call). The carriage carries the
welded + bindability gates; the entry points hold *no* reflection logic themselves.

**Resolution = stitch vs tack.** The carriage delegates the *which entities participate*
decision to its `Resolution` policy (in `welder::detail`), keeping it separate from *how*
they're emitted (the carriage body) and *whether* they're representable (the bindability
gate, shared by both). `marker_resolution` → **stitch welding** (honor `weld`/`policy`/
marks; the default `welder::stitch_welding_carriage`). `greedy_resolution` → **tack
welding** (`welder::tack_welding_carriage`): ignore the `weld` markers entirely — every
reflectable type/function/global participates, namespaces recurse greedily
(`namespace_has_bindable`, the welded_for-free twin of `namespace_has_bound` in
bind_traits), and every public base is *flattened* (Resolution::is_native_base = false, so
no reliance on a base being separately registered) — for welding a third-party library
with no welder annotations. Bindability is **still enforced** in tack mode, so a
non-representable member reachable from a bound entity is a hard error (hatch with
`trust_bindable`); any `mark::exclude` that happens to be present is still honored (via
`member_bound`). The sol2/luacats free-function overload gathering
(`rods/lua/overloads.hpp` `function_overload_set`) is resolution-agnostic by gathering
siblings that share `fn`'s *welded-ness* — so an all-unmarked (tack) group gathers fully
and an all-welded (stitch) group is unchanged, without the selector knowing the resolution.

**Two seams, both defaulted.** (1) The **name override**: weld_type / weld_function /
weld_variable / weld_namespace_as_submodule all take an optional trailing `const char*
name`, used verbatim and taking precedence over any `weld_as` (threaded to the rod's
`add_*` primitives, which fall back to `name_of` on `nullptr`). (2) The **carriage**:
`welder::welder`'s third template argument, so a user can inject a replacement traversal
driver (stitch/tack/bespoke `basic_carriage<Resolution>`) while reusing the rods.
`welder::welder` itself is all-static/non-virtual, so "subclassing" it means a user's own
driver type inherits the entry points (plus the bound Rod/Style/Carriage via `rod_type`/
`name_style`/`carriage_type`) to compose bespoke routines — not runtime polymorphism.

**Struct convention (all rods).** Each `rod` struct reads as one unit: the
associated types up top, then a `protected:` block of framework-specific
implementation helpers (each prefixed `_`, e.g. `_needs_registration`,
`_def_function`, `_make_usertype`), then the `public:` emission primitives — the
`welder::rod` contract — written in terms of them. A
`static_assert(::welder::rod<rod>)` immediately follows the struct so a contract
mismatch is a local, named error rather than a deep instantiation failure. The
struct lives *directly* in `welder::rods::<name>` (no `detail` wrapper — it is the
public API); its framework-specific collaborators live in the same namespace.
Cross-rod maps that would otherwise be duplicated verbatim are factored into shared
headers instead of the struct: the Python dunder map into
`rods/python/operators.hpp`, the sol2 metamethod map into
`rods/lua/sol2/metamethods.hpp`, and the LuaCATS type map + document assembler into
`rods/lua/luacats/{type_map,document}.hpp`.

`B` provides:

- **Associated:** `static constexpr lang language;` the target language;
  `using module_type = …;` the module handle; `template<class T> static constexpr
  bool has_native_caster;` — the `caster_oracle` leaf: is `T` convertible *without*
  welder registering a class for it? (false ⇒ welder requires `T` welded). This is
  the one bindability fact the core can't know; the STL-wrapper recursion in
  `bindable.hpp` is shared.
- **Type binding:** `make_class<T, Bases…>`, `add_default_ctor`, `add_constructor<Ctor>`,
  `add_aggregate_constructor<T>`, `add_field<Mem, Style>`, `add_method<Fn, Style>`,
  `add_static_method<Fn, Style>`, `add_operator<Fn>`, and `consteval special_method_name(op)`
  (the operator→target-name map, e.g. pybind's `operator+`→`__add__`; nullptr =
  not exposed, which also gates operator eligibility in the driver).
- **Enum binding:** `make_enum<E>`, `add_enumerator<Enum, Style>`, `finish_enum<E>` (the
  whole-enum finalizer, e.g. pybind's `export_values()` for unscoped enums).
- **Namespace/module binding:** `open_module`→ a per-(sub)module *session* (rod
  scratch state — pybind uses it to batch live variable properties), `set_module_doc`,
  `add_function<Fn, Style>(m, name=nullptr)`, `add_variable<Var, Style>(m, session,
  name=nullptr)` (const→snapshot, else a live property), `add_submodule`, `close_module`
  (finalize the session). The trailing `const char* name` on `add_function`/`add_variable`
  is the verbatim override (nullptr → `name_of`); the semi-manual carriage entry points
  thread the user's `weld_function`/`weld_variable` name through it, the namespace walk
  passes nullptr.

**Naming (name styling + `weld_as`).** Every name-producing hook takes a trailing
`class Style` (a `naming::name_style`, defaulted to `naming::none`); the rod resolves
its own name via `::welder::name_of<Ent, language, Style, ent_kind::…>()`, which
applies a `[[=welder::weld_as]]` verbatim override first and otherwise calls the
style's per-kind hook (`transform_method`, `transform_field`, …). A **call-site name
override** (the `const char* name` on weld_type / weld_function / weld_variable /
weld_namespace_as_submodule) beats even `weld_as`: it is passed as the resolved name
directly (class/enum/submodule) or threaded to `add_function`/`add_variable`, which use
it in place of `name_of`. The driver threads `welder::welder<Rod, Style>`'s Style down
and pre-styles the names it owns (class/enum → `make_class`/`make_enum`; submodule →
`add_submodule`), so a rod never re-derives naming policy. `add_operator` keeps the fixed operator→special-name map
(not styled). The core machinery is `<welder/naming.hpp>`; the shipped Python mix is
`welder::rods::python::pep8` (`rods/python/naming.hpp`). A type rename (style or
`weld_as`) propagates into the LuaCATS stub's *type references / base lists* too: the
type map still emits the raw C++ name (it sees only a `std::meta::info`, no Style /
`weld_as`), but `make_class`/`make_enum` register each type's raw→styled name into the
`document`, and `render()` reconciles references in one final pass
(`document.hpp` `apply_type_renames`) — deferring to render means declaration order is
irrelevant.

The concept statically checks the associated types and the module machinery; the
class/per-member hooks are templated on a reflection, so they are
contract-by-documentation, enforced when the driver instantiates. The nanobind
rod is nearly a copy of the pybind11 one (same class-handle model), diverging
only where nanobind's API does — `def_rw`/`def_ro`, `nb::init`, a
placement-`__init__` aggregate factory, module docstrings via `__doc__`, the
`is_base_caster` bindability probe, and `is_arithmetic` enums (Python `IntEnum`, to
match pybind11's int-convertible enums). Its one gap is multiple inheritance:
nanobind binds a single base per class, so a multi-base diamond binds under
pybind11 but not nanobind.

The **sol2 (Lua)** rod implements the same ~16 primitives against sol2, and is
where the emission contract stretches furthest from Python — the divergences are
the interesting part:
- **`module_type = sol::table`** (a Lua module is a table); the borrowed
  `lua_State*` is viewed with `sol::state_view`. `open_module`/`close_module` are
  no-ops (empty session) — namespace variables bind eagerly as snapshots.
- **caster oracle** reads `sol::lua_type_of<T>` (a `userdata` classification ⇒
  needs registration); enums are forced needs-registration so a welded enum's
  name→value table is required, matching the Python rods.
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
  rod recomputes the transitive set itself rather than using the core's
  *nearest*-ancestor `native_base_types`. It supports multiple + virtual bases (the
  diamond binds, unlike nanobind).
- **enums are name→value tables** (Lua has no enum type); an unscoped enum's names
  are also mirrored onto the enclosing module.
- **no runtime docstrings** (`doc`/`returns` ignored — their home is the
  `welder::rods::luacats::rod` LuaCATS stub rod, below). **Overloaded methods, static
  methods, free functions and operators are grouped into one `sol::overload(…)`**:
  sol2 stores one value per name/metamethod slot, so the rod gathers a name's
  overloads (mirroring how it already gathers a type's constructors "all at once")
  rather than letting the last registered win. Grouping happens in the sol2 rod
  (`method_overloads`/`operator_overloads`/`function_overloads` re-invoke the core
  selection predicates); the driver still visits overloads one at a time, which suits
  pybind11's incremental `.def`.
- entry point: `WELDER_MODULE(ns, sol2)` (from `rods/lua/sol2/module.hpp`) emits
  `extern "C" luaopen_<ns>` returning the module table.

The **LuaBridge3 (Lua)** rod is the second Lua runtime rod — same language, a very
different framework, which is where the emission contract stretches in new ways:
- **A move-based fluent registrar vs. the driver's stable handle.** LuaBridge3
  registers through `getGlobalNamespace(L).beginNamespace(…).beginClass<T>(…)…` where
  `beginClass`/`beginNamespace` *move-consume* their parent and only one registrar may
  be "active". welder's driver instead holds a `module_type& m` and mutates class/module
  handles across many separate `add_*` calls. The bridge is a **re-open-by-path** model:
  `module_type` is a light, copyable `{lua_State*, namespace-path}`, and every emission
  primitive re-walks the namespace chain in one chained expression and lets it unwind.
  `beginNamespace` reuses an existing namespace table (no wipe) and `beginClass<T>`
  re-opens a class preserving prior registration, so the repeated open/close is correct
  (a few extra table lookups per member, at load time only).
- **module_type is a named `_G` namespace.** LuaBridge3 registers into globals, so the
  `luaopen_` macro builds the module under `_G[<ns>]`, returns that table, and clears the
  `_G` binding (`require` wants a table, not a global). module_type is copyable
  (no live stack ownership), and `add_submodule` just extends the path.
- **caster oracle** reads `luabridge::detail::IsUserdata<T>` (true for any class type ⇒
  needs registration); enums forced needs-registration, matching sol2.
- **operators → metamethods by NAME string** (`addFunction("__add", …)`), from the shared
  `rods/lua/metamethods.hpp`. **`operator[]` is special**: it maps to `__index`, which
  LuaBridge3 reserves for member/property resolution, so it is registered as the
  `addIndexMetaMethod` *fallback* (consulted first, returns nil for non-subscript keys so
  member access still resolves). The fallback also coerces a stringified numeric key
  (LuaBridge3's index metamethod runs `lua_tostring` on the key first).
- **constructors, all at once** (like sol2) in `make_class`: `addConstructor<Sig…>()`
  for the call form `T(…)` **plus** a variadic `.new` static function over `make_object`
  factories for the idiomatic `T.new(…)` — the two forms sol2 also exposes; the driver's
  per-constructor hooks are no-ops. Aggregates ride C++26 parenthesized init.
- **inheritance: nearest welded ancestors** (like pybind11, *unlike* sol2's full closure)
  via `deriveClass<T, Base…>` — LuaBridge3 chains `__index` transitively and computes each
  base's cast offset. It supports **non-virtual** multiple inheritance but **not virtual
  bases** (its cast-offset is plain pointer arithmetic that a virtual base breaks —
  registering one crashes at load), so the shared *virtual* diamond is gated off for it
  (like nanobind's single-inheritance gating).
- **enums are nested value namespaces** (`E.Value` via `addVariable`); an unscoped enum's
  names are also mirrored onto the module. A blanket `Stack<E> : Enum<E>` specialization
  (in `namespace luabridge`, in the rod header) makes enums cross as integers.
- **namespace variables** are *easier* than sol2: const → `addVariable` snapshot; mutable
  → native `addProperty` get/set (no metatable proxy). `open_module`/`close_module` and the
  session are no-ops.
- entry point: `WELDER_MODULE(ns, luabridge)` (from `rods/lua/luabridge/module.hpp`).

**Rod namespaces.** Each rod lives in `welder::rods::<name>` and exposes one struct,
`rod`: `welder::rods::pybind11::rod` and `welder::rods::nanobind::rod` (both target
`lang::py`), `welder::rods::sol2::rod` and `welder::rods::luabridge::rod` (both target
`lang::lua`). Inside the Python rods,
unqualified `pybind11` / `nanobind` would resolve to the rod namespace, so each
aliases `namespace py = ::pybind11;` / `namespace nb = ::nanobind;` and uses `py::`
/ `nb::` throughout. `welder::rods::sol2` does *not* shadow the library's `::sol`
namespace, so it uses `sol::` directly (no alias); `welder::rods::luabridge` *would*
shadow `::luabridge`, so it aliases `namespace lb = ::luabridge;` (like the Python rods).
The **`welder::rods::luacats::rod`**
stub rod also targets `lang::lua` (it emits the Lua *type stub*, so it reflects the
same `weld(…, lang::lua)` types), but is a *build-time text emitter*, not a runtime
binding — see below and `docs-and-doxygen.md`. (`welder::rods` is deliberately a
grouping namespace with room for non-rod helpers alongside the rods, e.g.
`welder::rods::python` / `welder::rods::lua`.)

Complex/custom type conversions are intended to be registered per-rod via each
framework's own mechanisms, separately from core resolution — design pending.

## CMake targets

- **`welder::headers`** — INTERFACE, the header-only core (include path + flags).
- **`welder::module`** — STATIC, builds `src/welder/welder.cppm`; provides `import welder;`.
  (Target names keep their framework spelling — `welder::pybind11` etc. — even
  though the rod type is now `welder::rods::pybind11::rod`.)
- **`welder::pybind11`** — INTERFACE, the pybind11 rod (links headers + pybind11 + Python).
  Gated by `WELDER_BUILD_PYBIND11`.
- **`welder::nanobind`** — INTERFACE, the nanobind rod. nanobind compiles its
  runtime *into* each extension via its own `nanobind_add_module()`, so this target
  only surfaces the welder + nanobind headers; an extension is still created with
  `nanobind_add_module`. Gated by `WELDER_BUILD_NANOBIND`.
- **`welder::sol2`** — INTERFACE, the sol2 Lua rod. A loadable Lua C module must
  resolve `lua_*` from the *host* interpreter and not bundle its own runtime, so this
  target surfaces only the sol2 + Lua *headers* (it does not link `lua::lua`). Create
  an extension with `welder_sol2_add_module()` (cmake/WelderSol2Module.cmake), which
  sets the bare `<name>.so`, the host-symbol link model (`-undefined dynamic_lookup`
  on macOS), and `CXX_SCAN_FOR_MODULES OFF` (sol2's `<luaconf.h>` can't see
  `LLONG_MAX` under p1689 module scanning — a header-unit macro-visibility issue, so
  a Lua binding TU is header-only, never `import welder;`). Gated by
  `WELDER_BUILD_SOL2`; needs `sol2` + `lua` (conan `with_sol2`).
- **`welder::luabridge`** — INTERFACE, the LuaBridge3 Lua rod. Same host-symbol,
  header-only story as `welder::sol2` (surfaces the LuaBridge3 + Lua headers, links no
  `liblua`). Create an extension with `welder_luabridge_add_module()`
  (cmake/WelderLuaBridgeModule.cmake). **LuaBridge3 has no Conan package and ships no
  CMake config**, so it is provisioned at the top level as the `LuaBridge` target:
  *consumers* bring their own via `find_package(LuaBridge3)` / `-DWELDER_LUABRIDGE_DIR`;
  *our* build FetchContents a pinned commit (`WELDER_LUABRIDGE_GIT_TAG`, gated by
  `WELDER_LUABRIDGE_FETCH`, default = building tests). Lua itself comes from the
  system/user via its **own** version knobs — `WELDER_LUABRIDGE_LUA_VERSION` /
  `WELDER_LUABRIDGE_LUA_DIR` (defaulting to the sol2 ones), since LuaBridge3 supports
  newer Lua minors (5.5+) than sol2 can — with the same segfault-guarding minor check.
  Gated by `WELDER_BUILD_LUABRIDGE`.
- **`welder::luacats`** — INTERFACE, the LuaCATS `---@meta` stub rod. Unlike the
  runtime rods it depends on neither a framework nor a language runtime (pure
  reflection → text), so it is **unconditional** — just `welder::headers`. Emit a
  stub with `welder_luacats_generate_stub()` (cmake/WelderLuaCATSStub.cmake), which
  builds a `WELDER_LUACATS_MAIN` generator executable and runs it into `<name>.lua`
  as an ALL target (`CXX_SCAN_FOR_MODULES OFF`, matching the Lua-side TUs).

Reflection/module flags are isolated in the `welder_flags` INTERFACE target and
gated on compiler id, so nothing gcc-specific leaks into the public targets.
