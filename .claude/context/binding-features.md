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
makes `weld_type<Welded<int>>(m, "name")` legitimate today — the explicit name is
required (a specialization `has_identifier` == false; the `identifier_of` name
default would throw).

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
- **`weld_as`** (`annotations.hpp`, std-free): the ultimate per-entity override. On an
  entity, `[[=welder::weld_as("name")]]` (all langs) or `[[=welder::weld_as(lang,
  "name")]]` (one lang, repeatable) forces the target name **verbatim** — it never
  flows through `Style`. Stored as a templated `weld_as_spec<N>` (mask + `fixed_string`,
  like `doc_spec`); read by `weld_as_of<Ent, L>()`, which `name_of` checks first.
- **Caveat (LuaCATS):** the stub styles *declarations*, but type *references* /
  `---@class` base lists still use the C++ type name, so a style/`weld_as` renaming a
  **type** is not propagated into them. `pep8` keeps type names PascalCase, so it
  doesn't bite. `luacats::rod::generate<Ns, Style>` forwards a style so a styled stub
  can match a styled sol2 binding.
- **Tests:** `tests/core/naming.cpp` (compile-only static_asserts: word-splitting,
  restyle across conventions, `name_of`/`weld_as_of` incl. per-language overrides and
  style-bypass). All four rods compile against the threaded contract (`rod_probe.cpp`
  updated with the trailing `Style` hook param).

## Rods
Three rods implement every feature above from the same driver: **pybind11**
(`welder::rods::pybind11::rod`), **nanobind** (`welder::rods::nanobind::rod`) — both
`lang::py` — and **sol2** (`welder::rods::sol2::rod`, `lang::lua`). nanobind's one
behavioral gap is multiple
inheritance (single base per class), so a multi-base diamond binds under pybind11 +
sol2 but not nanobind. Enums bind as `py::native_enum` (pybind11 → stdlib
`enum.IntEnum`) / an `is_arithmetic` `nb::enum_` (nanobind → Python `IntEnum`) / a
name→value **table** (sol2 — Lua has no
enum type).

## Lua specifics (sol2)
The same annotated cases bind for `lang::lua`; the Lua-only differences (see
`architecture.md` for the full list):
- **Operators → Lua metamethods**, a smaller/asymmetric map: `+`/`-`(binary/unary)/
  `*`/`/`/`%` → `__add`/`__sub`/`__unm`/`__mul`/`__div`/`__mod`; `==`→`__eq`,
  `<`→`__lt`, `<=`→`__le`; **`!=`, `>`, `>=` map to *nothing*** — Lua derives `~=`,
  `>`, `>=` from `__eq`/`__lt`/`__le`. `^`(XOR)→`__bxor`, `&`/`|`/`~`/`<<`/`>>` →
  the bitwise metamethods, all `#if LUA_VERSION_NUM >= 503`. `[]`→`__index` (a sol2
  fallback that coexists with member/method access), `()`→`__call`.
- **Overloaded methods/functions/operators are grouped** into one `sol::overload(…)`
  (sol2 stores one value per name / metamethod slot), so every overload dispatches at
  call time rather than the last registered winning. The grouping is done in the sol2
  rod — the driver visits each overload individually (suiting pybind11's chained
  `.def`), and the rod gathers a name's siblings with the core selection
  predicates, exactly as it already gathers a type's constructors. A same-named member
  in a derived class still hides the base's (C++ name-hiding), unchanged.
- **Namespace variables snapshot** at load time (const and mutable alike); live
  get/set over a C++ global is a planned enhancement.
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
overload grouping, and the LuaCATS stub emitter now are.) Remaining sol2 rod
enhancement noted above: live namespace variables (and LuaJIT's 5.1 operator-map
branch). LuaCATS stub: overloaded methods/constructors/free functions now render as
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
