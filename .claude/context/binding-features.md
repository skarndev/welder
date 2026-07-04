# Binding features (pybind11 backend) — implementation detail

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
The operator→name map is the backend's `special_method_name(op)` (nullptr = not
exposed, which also gates operator eligibility in the driver).

## Enums → `py::enum_`
A welded enum (scoped or unscoped) binds via `bind<E>` (dispatched from the public
`bind<T>` by `is_enum_v`) or as a namespace/module member; the driver is
`backend.hpp` `bind_enum`, the backend hooks `make_enum` / `add_enumerator` /
`finish_enum`. Each **enumerator resolves like a data member** — the enum's
`policy` (default automatic) plus per-enumerator `exclude`/`include` marks decide
what binds (via the same `member_bound`); NB the C++ grammar puts an enumerator's
annotation *after* its name (`South [[=welder::mark::exclude]]`). Excluding an
enumerator does not renumber the rest. An **unscoped** enum also `export_values()`
(enumerators visible unqualified on the enclosing module, mirroring C++); a
**scoped** enum stays `E.Value`. The enum `doc` becomes the Python docstring;
per-enumerator docs aren't supported (pybind11 `.value()` takes none). An
enum-typed member/parameter binds because the enum is welded (bind the enum first,
like a welded base). Tested: `tests/pybind11/cpp/enums.hpp` + `test_enums.py`.

## Inheritance from public bases
`weld` is a *discovery marker* (an independently-registered, module-discoverable
entity), not an inheritance directive: the most-derived type's `weld` drives which
languages bind, and a base need not be welded. A **welded** base → a native
pybind11 base (`class_<T, Base...>`; bind it separately, first), including the
nearest welded ancestors reached *through* non-welded ones (deduplicated). A
**non-welded** base → a C++ mixin whose eligible members are flattened in
recursively (honoring its own marks/policy). Virtual diamonds work; a non-virtual
diamond with a shared welded base is a C++ ambiguity (not worked around).

## Whole-namespace binding — `bind_namespace<^^ns>(m)`
`weld` gates *leaf entities only* (class type / free function / namespace-scope
variable; namespaces are never welded); the namespace `policy` (default automatic)
+ member marks then resolve. Binds classes (`bind<T>`), free functions (overloads
included), and namespace variables as module attributes — a **value snapshot if
const/constexpr, else a live get/set property** over the C++ global (via a
`ModuleType` `__class__` swap). A **nested namespace** resolves under the
*parent's* policy (no weld; automatic recurses unless excluded, opt_in only if
included — keeps `detail`/`impl` out) and becomes a submodule when it holds bound
content. Declaration order.

## Whole-module binding — `build_module<^^ns>(m, pre, post)`
Fills an *existing* module (pre hook → `bind_namespace` → post hook; namespace
`doc` → module doc). The C entry symbol `PyInit_<name>` must be preprocessor-pasted,
so the backend-agnostic `WELDER_MODULE(ns, backend)` macro (`module.hpp`) wraps it
(namespace token = module name, optional trailing `{ }` post-glue with the module
handle in scope as `module`). One `WELDER_MODULE` per backend per TU; two Python
backends collide (both emit `PyInit_<name>`).

## Template ↔ annotation semantics
Locked in by `tests/core/template_annotations.cpp` (compile-only static_asserts):
annotations on a template *declaration* are readable through every
**instantiation** — with primary / partial / explicit-specialization precedence,
and including member, parameter and `weld`/mark annotations; `substitute()`d
function/variable-template instantiations carry them too. Only the *uninstantiated*
template (or concept) reflection refuses `annotations_of` (P2996 restriction) — but
any instantiation handed to welder has full docs, and `weld` on a class template
makes `bind<Welded<int>>(m, "name")` legitimate today — the explicit name is
required (a specialization `has_identifier` == false; the `identifier_of` name
default would throw).

## Not yet implemented
Properties and additional languages (Lua, …) are designed-for but not yet
implemented. (Enums and custom type converters now are.)
