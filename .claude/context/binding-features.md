# Binding features (pybind11 rod) — implementation detail

Read when: working on what binds — data members, constructors, operators, enums,
inheritance, namespaces, whole modules, or template↔annotation semantics. This is
the impl/test-location companion to the user guide (`docs/content/guide/*`); the
guide has the user-facing walkthrough, this has driver hooks + test files.

All honor exclude/include/policy via `reflect.hpp` `member_bound`.

## Data members & constructors
Public data members (a mutable member read/write via `def_readwrite`; a **const**
member read-only via `def_readonly` — `def_readwrite`'s setter won't compile on
const); a member's `[[=welder::doc]]` becomes its property `__doc__` (see
`docs-and-doxygen.md`). Constructors (default + each public
non-copy/move ctor → `pybind11::init<...>`; plus, for a baseless **aggregate**,
a synthesized field constructor that brace-inits it, giving Python `T(f0, f1, …)`
— only when every field binds, since aggregate init is positional/all-or-nothing);
methods, static methods, overloads. Function / method / constructor **parameter
names** reach Python as keyword arguments (`py::arg`) when every parameter of that
signature is named.

**Member resolution marks:** `exclude`/`include` plus `mark::only(lang...)` — the
closed-world mark: the COMPLETE set of languages the member binds for; under
`opt_in` it doubles as the opt-in; `exclude` still beats it; repeats union; bare
form diagnosed at resolution (reflect.hpp `member_bound`, anchor fn
`bare_mark_only_is_meaningless_…`). Cases: `resolution.hpp` `only_py` /
`only_then_excl` / `only_lua` + test_resolution.py / resolution_spec.lua.

**Returned handles (the mixing story):** every `weld_*` forwards its rod hook's
return — `weld_type` → the class/enum handle (`py::class_<T>` / `nb::class_<T>` /
`sol::usertype<T>` / luabridge `class_handle`), `weld_function` → the bound
function object on pybind11/nanobind (`m.attr(name)`) and sol2 (table entry;
invalid `sol::object` for a non-leader overload), void on luabridge/text rods;
`weld_namespace_as_submodule` → the submodule handle; `weld_variable` forwards
but all shipped rods return void. Carriage bind_function/bind_variable forward
(`if constexpr is_void` dance around the session in bind_variable). Cases:
`chaining.hpp` (+ per-backend `WELDER_TEST_CHAIN_*` seams) + test_chaining.py /
chaining_spec.lua.

## Overloaded operators → Python special methods
A *member* operator binds under its dunder (`operator+` → `__add__`, `operator==`
→ `__eq__`, `operator[]` → `__getitem__`, `operator()` → `__call__`, …), unary vs
binary told apart by arity so the two `operator-` forms map to `__neg__` /
`__sub__`. Arithmetic / bitwise / comparison / call / subscript are covered;
in-place compound assignment (`operator+=`) is intentionally not mapped (Python
falls back to `a = a + b` via `__add__`), nor are `<=>`, `&&`, `||`, `++`, `--`,
`operator=` (special member). *Free* (non-member) operators aren't bound yet.
The operator→name map is the rod's `special_method_name(op)` (nullptr = not
exposed, which also gates operator eligibility in the driver).

## Enums → `enum.IntEnum`
A welded enum (scoped or unscoped) binds via `weld_type<E>` (dispatched from the
public `weld_type<T>` by `is_enum_v`) or as a namespace/module member; the driver is
`welder.hpp` `bind_enum`, the rod hooks `make_enum` / `add_enumerator` /
`finish_enum`. Each **enumerator resolves like a data member** — the enum's
`policy` (default automatic) plus per-enumerator `exclude`/`include` marks decide
what binds (via the same `member_bound`); NB the C++ grammar puts an enumerator's
annotation *after* its name (`South [[=welder::mark::exclude]]`). Excluding an
enumerator does not renumber the rest. An **unscoped** enum also `export_values()`
(enumerators visible unqualified on the enclosing module, mirroring C++); a
**scoped** enum stays `E.Value`. The pybind11 rod binds via `py::native_enum`
(a stdlib `enum.IntEnum`; `py::enum_` is discouraged as of pybind11 3.0) — it is
move-only and needs an explicit `.finalize()`, so `make_enum` returns a
`unique_ptr` handle (the movable value `bind_enum` returns) and `finish_enum`
finalizes. The enum `doc` becomes the Python docstring; welder doesn't currently
surface per-enumerator docs. An
enum-typed member/parameter binds because the enum is welded (bind the enum first,
like a welded base). Tested: `tests/common/cpp/enums.hpp` + `tests/python/test_enums.py`.

## Inheritance from public bases
`weld` is a *discovery marker* (an independently-registered, module-discoverable
entity), not an inheritance directive: the most-derived type's `weld` drives which
languages bind, and a base need not be welded. A **welded** base → a native
pybind11 base (`class_<T, Base...>`; bind it separately, first), including the
nearest welded ancestors reached *through* non-welded ones (deduplicated). A
**non-welded** base → a C++ mixin whose eligible members are flattened in
recursively (honoring its own marks/policy). Virtual diamonds work; a non-virtual
diamond with a shared welded base is a C++ ambiguity (not worked around).

## Virtual-method overriding (trampolines) — Python rods
Files: `src/welder/rods/python/trampoline.hpp` (shared, backend-neutral) +
`src/welder/rods/python/{nanobind,pybind11}/trampoline.hpp` (per-backend dispatch +
macros). Both Python rods support it; the user's trampoline source is identical, only
the backend `trampoline.hpp` include differs (nanobind adds a `detail::trampoline<N>`
storage member; pybind11 needs none — `get_override(this,name)`). Tests:
`tests/common/cpp/overridable.hpp` ↔ `tests/python/test_trampoline.py` (runs on both;
skips if the `overridable` submodule is absent); neg-compile
`tests/python/{nanobind,pybind11}/cpp/neg/{virtual_needs_trampoline,trampoline_missing_override}.cpp`.

**Why hand-authored:** a trampoline is a C++ subclass with one `override` per
virtual; generating those *declarations* needs member injection, absent from P2996
(`define_aggregate` is data-members-only), and the vtable forces each override to
share the base method's exact name. welder automates everything *around* it via
reflection; the declarations stay hand-written.

**Inherited virtuals:** the slot set (`overridable_virtuals`, behind
`virtual_slot_count` / `has_virtual_methods` / `trampoline_covers`) walks the whole
base chain, not just `members_of` (own members) — a virtual a welded type merely
*inherits* is still an overridable slot, so a derived welded type's trampoline must
cover the inherited virtuals too (a Python subclass can override them, and dispatch
runs through the derived type's own trampoline, not the base's). Slots dedup by name +
`type_of`, keeping the most-derived declaration; that decl's `bind_flat` mark governs.
Regression: `Bird : Animal` in `overridable.hpp` (inherits speak/legs, adds fly) with
`test_derived_class_overrides_{inherited,own}_virtual`.

**Discovery (two forms; explicit wins):** virtuals are auto-detected
(`virtual_slot_count` > 0, destructor and per-method `bind_flat` excluded). The
`T→trampoline` mapping resolves as: (1) explicit `trampoline_for<T>` — a specializable
`std::meta::info` var template (the `trust_bindable` pattern); else (2) annotation —
`[[=welder::rods::python::trampoline]]` on `PyT`, discovered by `scanned_trampoline_of`
scanning `parent_of(^^T)` for a `trampoline`-annotated type whose `bases_of` includes
`T`. No global type enumeration in reflection ⇒ the scan needs a known scope, so the
annotation form requires `PyT` in `T`'s namespace; the explicit form has no such
constraint (third-party / cross-namespace) and disambiguates >1 candidate (ambiguity
is a `static_assert` in make_class). The var-template read needs a *type* param (can't
splice a function param), so make_class reads `trampoline_for<T>` and passes `^^T` to
the info-taking scan. NB: naming the marker `trampoline` collided with `trampoline`
params in `declares_override`/`trampoline_covers` under -Wshadow → renamed to `tramp`.

**Abstract bases (pure virtuals):** an abstract `T` is not `is_default_constructible`,
so the carriage would register no ctor and even a subclass would be uninstantiable
("no constructor defined!"). Fix: the carriage's default-ctor gate uses the optional
`B::construction_type<T>` (Python rods → the trampoline if registered, else `T`;
`construction_type_of` in the shared header) — detected via `requires`, so Lua rods
fall back to `is_default_constructible_v<T>` unchanged. `nb::init<>()`/`py::init<>()`
already construct the alias for abstract T. Consequence (framework behavior, not
welder's): the base becomes constructible and an unoverridden pure virtual raises at
call time (`RuntimeError`), not at construction. `is_overridable_virtual` counts pure
virtuals, so coverage requires them in the trampoline.

**Gate (strict):** the Python rod's `make_class` (`rod.hpp`) branches on
`has_virtual_methods(^^T)`: trampoline registered → splice `class_<T, PyT, Bases…>`
via `_make_class`'s new `Trampoline` param + `static_assert(trampoline_covers(...))`
(every overridable virtual is redeclared in `PyT`, matched by name + `type_of` — full
signature incl. cv/ref, so overloads/covariant returns don't false-match); else
`static_assert(bound_flat(^^T))` — a virtual type must register a trampoline or carry
`[[=welder::rods::python::bind_flat]]` (type-level = whole type flat; per-method =
that virtual stays a plain bound method, out of slot count + coverage).

**Dispatch:** `WELDER_PY_OVERRIDE(fn, args…)` → each backend's
`override_dispatch<^^welder_py_base::fn>` (name/return-type/pure-ness from reflection).
pybind11's `get_override(const T*, name)` keys the Python-object lookup off `typeid(T)`
— the *static* pointer type — so the macro casts `*this` to `welder_py_base` (the
**registered** welded type, `class_<T, Trampoline, …>`), **not** the virtual's declaring
class. For an inherited virtual those differ; casting to the declaring base looks the
instance up under the wrong registration and silently misses the override (C++ sees the
base impl). nanobind is unaffected — it dispatches through its own trampoline storage +
`detail::ticket`, not `typeid`. Base fallback is a **textually
qualified** `welder_py_base::fn(args)` lambda — NOT `self.[:Fn:]()`, which splices to
a *virtual* call and infinitely recurses. `WELDER_PY_TRAMPOLINE(Base)` injects the
`welder_py_base` alias + inherited ctors, plus (nanobind only) a
`nb::detail::trampoline<slot_count>` storage member; pybind11 uses `get_override` +
`detail::cast_safe`, no storage. Reference/pointer returns are `static_assert`-rejected
(lifetime). Macros are neutrally named so one trampoline source compiles under either
Python rod.

**Generating trampolines (`welder::rods::trampolines::rod`).** The hand-written
trampoline is mechanical, so a build-time text-emitting rod emits it — the Python
analogue of the LuaCATS stub rod, over the same driver. Files:
`src/welder/rods/python/trampolines/{document,rod,module}.hpp`; CMake helper
`cmake/WelderTrampolines.cmake` (`welder_generate_trampolines()`); target
`welder::trampolines`. Only `make_class<T>` emits (a `struct … : T { WELDER_PY_TRAMPOLINE;
one WELDER_PY_OVERRIDE per overridable virtual };` + a `trampoline_for<T>` spec), skipping
a whole-type `bind_flat` and types with no overridable virtuals; every other rod hook is a
no-op and `has_native_caster` is permissive (it reproduces only virtual *signatures*). Each
override **splices** the base virtual's reflected return/param types
(`[: std::meta::type_of(overridable_virtuals(^^T)[k]) :]`), so signatures match by
construction — validated across 35/36 hostile shapes (`scratchpad`), the lone gap being a
**C-variadic** virtual (P2996 has no ellipsis query → `is_c_variadic` reads the display
string and emits a `static_assert` unless `bind_flat`). The generated header is
backend-neutral (neutral macros), so one header serves both Python rods. Tests:
`tests/common/cpp/gen_trampolines.hpp` (welded types, no hand trampolines) +
`tests/python/gen_trampolines_gen.cpp` (the generator) → `test_gen_trampolines.py`, wired
into both `bindings.cpp` via `tests/python/CMakeLists.txt` (`welder_generate_trampolines`)
so both extensions compile the *same* generated header — a cross-rod consistency check.

## Whole-namespace binding — `weld_namespace<^^ns>(m)`
`weld` gates *leaf entities only* (class type / free function / namespace-scope
variable; namespaces are never welded); the namespace `policy` (default automatic)
+ member marks then resolve. Binds classes (`weld_type<T>`), free functions (overloads
included), and namespace variables as module attributes — a **value snapshot if
const/constexpr, else a live get/set property** over the C++ global (via a
`ModuleType` `__class__` swap). A **nested namespace** resolves under the
*parent's* policy (no weld; automatic recurses unless excluded, opt_in only if
included — keeps `detail`/`impl` out) and becomes a submodule when it holds bound
content. Declaration order.

## Whole-module binding — `weld_module<^^ns>(m, pre, post)`
Fills an *existing* module (pre hook → `weld_namespace` → post hook; namespace
`doc` → module doc). The C entry symbol `PyInit_<name>` must be preprocessor-pasted,
so the rod-agnostic `WELDER_MODULE(ns, rod)` macro (`module.hpp`) wraps it
(namespace token = module name, optional trailing `{ }` post-glue with the module
handle in scope as `module`). One `WELDER_MODULE` per rod per TU; two Python
rods collide (both emit `PyInit_<name>`).

## Template ↔ annotation semantics
Locked in by `tests/core/template_annotations.cpp` (compile-only static_asserts):
annotations on a template *declaration* are readable through every
**instantiation** — with primary / partial / explicit-specialization precedence,
and including member, parameter and `weld`/mark annotations; `substitute()`d
function/variable-template instantiations carry them too. Only the *uninstantiated*
template (or concept) reflection refuses `annotations_of` (P2996 restriction) — but
any instantiation handed to welder has full docs, and `weld` on a class template
makes `weld_type<Welded<int>>(m, "name")` legitimate — the explicit name is
required (a specialization `has_identifier` == false), and it WORKS because the
driver/rods resolve names through `name_of_or` (naming.hpp), whose `name_of`
fallback is compiled only when the entity is statically nameable (identifier or
weld_as); a missing override then throws std::invalid_argument at binding time.
(Previously `name ? name : name_of<…>()` constant-evaluated the consteval
`name_of` unconditionally and hard-errored even WITH the override.) Function-
template instantiations bind the same way:
`weld_function<std::meta::substitute(^^ns::fn, {^^int})>(m, "name")`. Runtime
coverage: cookbook recipe 06 (examples/cookbook/06-templates); compile lock:
tests/core/naming.cpp name_of_or asserts.

## Naming conventions & `weld_as`
Two pieces, both rod-agnostic (they live in the core / driver, so every rod gets
them for free):
- **Name styles.** `welder::welder<Rod, Style=naming::none>` takes a *name style* as
  its second template arg. The driver threads `Style` into every name-producing rod
  hook (`add_field<Mem, Style>`, `add_method<Fn, Style>`, `add_enumerator`,
  `add_function`, `add_variable`, `add_static_method`) and pre-styles the names it
  owns itself (class/enum → `make_class`/`make_enum`, submodule → `add_submodule`).
  Each name resolves through `::welder::name_of<Ent, L, Style, ent_kind::K>()`
  (`naming.hpp`). A style is a set of **per-kind** `static consteval std::string
  transform_*(std::meta::info)` hooks (`transform_class`/`_enum`/`_enumerator`/
  `_method`/`_static_method`/`_function`/`_field`/`_variable`/`_submodule`) — the
  `naming::name_style` concept. Core helpers: `split_words`/`join_words`/`restyle`
  (split an identifier however spelled — underscores, camel humps, acronym runs — then
  re-join in a `case_kind`); styles `naming::{none, snake_case, pascal_case,
  camel_case, screaming_snake_case, kebab_case}` (`uniform<Kind>`). `none` is the
  default (identity). The shipped Python mix is `welder::rods::python::pep8`
  (`rods/python/naming.hpp`): PascalCase types, snake_case everything else,
  enumerators verbatim.
- **`weld_as`** (`annotations.hpp`, std-free): the ultimate per-entity override. The
  name is the **last** argument, preceded by zero or more `lang` markers:
  `[[=welder::weld_as("name")]]` (all langs), `…weld_as(lang, "name")` (one), or
  `…weld_as(lang, lang, …, "name")` (several at once); repeat the annotation for a
  different name per language. It forces the target name **verbatim** — never through
  `Style`. Stored as a templated `detail::weld_as_spec<N>` (mask + `detail::fixed_string`, like
  `detail::doc_spec`); read by `weld_as_of<Ent, L>()`, which `name_of` checks first. A pack
  can't precede a deduced trailing string, so the multi-marker form is a single
  forwarding-pack overload with two `detail` helpers (`weld_as_mask`/`weld_as_name`)
  that walk the args — mask the leading `lang`s, peel to the name (bound by reference
  so its extent survives). The bare all-languages `weld_as("name")` keeps its own
  more-specialized overload.
- **LuaCATS type references:** a type rename (style or `weld_as`) reaches the stub's
  type *references* / `---@class` base lists / container element types, not just
  declarations. The type map still emits the raw C++ name (it has only a
  `std::meta::info`), but `make_class`/`make_enum` register raw→styled into the
  `document` and `render()` reconciles references in one final pass
  (`document.hpp` `apply_type_renames`, tokenizing on the identifier+`.` class so a
  dotted name is remapped atomically) — order-independent because it runs after all
  types are declared. `luacats::rod::generate<Ns, Style>` forwards a style so a styled
  stub matches a styled sol2 binding.
- **Tests:** `tests/core/naming.cpp` (compile-only static_asserts: word-splitting,
  restyle across conventions, `name_of`/`weld_as_of` incl. per-language overrides and
  style-bypass); runtime `tests/python/test_naming.py` + `tests/lua/spec/naming_spec.lua`
  (styled binding via the `WELDER_TEST_STYLED_WELDER` seam); the LuaCATS reference
  reconciliation is covered by the `stub_gen.cpp` golden — `Shape`/`Box` carry a
  `weld_as` reached only through a base list and `vector`/`map` references. All four
  rods compile against the threaded contract (`rod_probe.cpp` updated with the trailing
  `Style` hook param).

## Rods
Four rods implement every feature above from the same driver: **pybind11**
(`welder::rods::pybind11::rod`), **nanobind** (`welder::rods::nanobind::rod`) — both
`lang::py` — and **sol2** (`welder::rods::sol2::rod`) + **LuaBridge3**
(`welder::rods::luabridge::rod`) — both `lang::lua`. Behavioral inheritance gaps:
nanobind is single-base-only, and LuaBridge3 supports non-virtual multiple inheritance
but **not virtual bases**, so a *virtual* diamond binds under pybind11 + sol2 but not
nanobind or LuaBridge3. Enums bind as `py::native_enum` (pybind11 → stdlib
`enum.IntEnum`) / an `is_arithmetic` `nb::enum_` (nanobind → Python `IntEnum`) / a
name→value **table** (both Lua rods — Lua has no enum type).

## Lua specifics (sol2 + LuaBridge3)
The same annotated cases bind for `lang::lua` under **both** Lua rods, asserted by the
*same* busted specs (selected via `WELDER_TEST_LUA_MODULE`). The Lua-only differences
below apply to both unless noted; where the two Lua frameworks diverge, LuaBridge3's
differences are called out (see `architecture.md` for the full per-rod list):
- **Operators → Lua metamethods**, a smaller/asymmetric map: `+`/`-`(binary/unary)/
  `*`/`/`/`%` → `__add`/`__sub`/`__unm`/`__mul`/`__div`/`__mod`; `==`→`__eq`,
  `<`→`__lt`, `<=`→`__le`; **`!=`, `>`, `>=` map to *nothing*** — Lua derives `~=`,
  `>`, `>=` from `__eq`/`__lt`/`__le`. `^`(XOR)→`__bxor`, `&`/`|`/`~`/`<<`/`>>` →
  the bitwise metamethods, all `#if LUA_VERSION_NUM >= 503`. `[]`→`__index` (a
  fallback that coexists with member/method access), `()`→`__call`. The operator→`__name`
  map is shared by both Lua rods (`rods/lua/metamethods.hpp`); sol2 pairs each name with
  its `sol::meta_function` slot, LuaBridge3 registers by the name string. **LuaBridge3
  `[]` divergence:** LuaBridge3 *reserves* `__index` for member/property resolution, so
  `operator[]` is registered as its `addIndexMetaMethod` *fallback* (consulted first,
  returns nil for non-subscript keys) and the adapter coerces LuaBridge3's stringified
  numeric key.
- **Overloaded methods/functions/operators are grouped** (each Lua slot holds one
  value): sol2 into one `sol::overload(…)`, LuaBridge3 into one variadic
  `addFunction(name, f1, f2, …)`. Either way every overload dispatches at call time
  rather than the last registered winning. The grouping is done in the rod — the driver
  visits each overload individually (suiting pybind11's chained `.def`), and the rod
  gathers a name's siblings with the shared `rods/lua/overloads.hpp` selectors, exactly
  as it already gathers a type's constructors. A same-named member in a derived class
  still hides the base's (C++ name-hiding), unchanged.
- **Constructors, all at once** (both Lua rods want the whole set): sol2 via
  `sol::constructors`, LuaBridge3 via `addConstructor<Sig…>()`. Both expose the call
  form `T(…)` **and** `T.new(…)` (LuaBridge3 adds `.new` as a variadic static function
  over factory functions). Aggregates ride C++26 parenthesized init.
- **Namespace variables: const snapshots, mutable live.** A const/constexpr variable
  binds as a value snapshot; a mutable one binds as a live get/set over the C++ global.
  sol2 has no per-variable property, so it routes the absent key through a metatable
  proxy (`__index`/`__newindex` → getter/setter closures accumulated in its `session`,
  installed by `close_module`, chaining any pre-existing metatable). **LuaBridge3 is
  simpler**: native `addProperty(name, get, set)` — no proxy, and its session is a
  no-op. Both match the Python backends. Asserted by `namespace_spec.lua`.
- **`doc`/`returns` are ignored at runtime** (no Lua `__doc__`) — they surface
  instead in the generated **LuaCATS stub** (`welder::rods::luacats::rod`; see
  `docs-and-doxygen.md` and build-test-run.md). The stub reflects the same welded
  Lua types through the same driver and writes a `---@meta` file with `---@field`/
  `---@param`/`---@return`/`---@class`/`---@enum`/`---@operator` tags plus the docs.
  The stub `---@operator` set mirrors the sol2 runtime metamethod map (arithmetic +
  bitwise + `call`), with two exceptions the language server can't name (`vm.OP_*_MAP`):
  **comparison** (`==`/`<`/`<=`) and **subscript** (`[]`) — sol2 binds them
  (`__eq`/`__lt`/`__le`/`__index`) but `operator_luacats` (type_map.hpp) drops them
  (they work at runtime, the stub just can't type them; emitting `---@operator
  eq/lt/le/index` makes the language server reject the stub with `unknown-operator`).
  The bitwise metamethods sol2 `#if`-gates to Lua ≥ 5.3 are emitted unconditionally
  (the stub carries no Lua headers, so version is the reader's `.luarc.json`).
Tested by the shared cases bound for `lua`, asserted by the busted specs in
`tests/lua/spec/*_spec.lua`.

## Not yet implemented
Properties (getter/setter pairs) are designed-for but not yet implemented; so are
further languages. (Enums, custom type converters, the Lua/sol2 rod, sol2
overload grouping, live sol2 namespace variables, and the LuaCATS stub emitter now
are.) Remaining sol2 rod enhancement: LuaJIT's 5.1 operator-map branch. LuaCATS stub: overloaded methods/constructors/free functions now render as
one documented `function` plus `---@overload fun(…)` lines (the primary — kept with
its full `@param`/summary docs — is the first overload carrying a doc); a **const**
member's read-only-ness is surfaced as a `(read-only)` description note, since
LuaCATS has no read-only/const field tag ([lua-language-server open request][ro]).
The generated stub is now validate-if-present linted by **lua-language-server**
(`stubcheck.luacats` CTest, the Lua analogue of the Python `stubcheck.<variant>`
mypy gate): `lua-language-server --check` over the emitted stub, gated on the tool
being found. The `.luarc.json` beside the stub forces the type/annotation
diagnostics that matter for a defines-but-never-uses `---@meta` file
(`undefined-doc-name`/`-class`, `unknown-operator` → `neededFileStatus: Any!`). It
was this lint that caught the invalid `---@operator eq/lt/le/index` emissions.

[ro]: https://github.com/LuaLS/lua-language-server/discussions/2379
