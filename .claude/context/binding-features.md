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
`docs-and-doxygen.md`). Constructors (default + each PARTICIPATING public
non-copy/move ctor → `pybind11::init<...>`; plus, for a baseless **aggregate**,
a synthesized field constructor that brace-inits it, giving Python `T(f0, f1, …)`
— only when every field binds, since aggregate init is positional/all-or-nothing);
methods, static methods, overloads. All the pieces reach the rod as ONE
`add_constructors<T, Ctors, HasDefault, Aggregate, Copyable>` call
(carriage-computed).
**NSDMI defaults on the synthesized field ctor** (bind_traits
`aggregate_required_arity` / `aggregate_defaults_from`): fields after the LAST
one without an NSDMI are the omissible suffix — Python attaches their values as
real keyword defaults (read off a value-initialized `T{}` probe at registration;
P2996 has no NSDMI-value query), the Lua rods emit one ctor arity per omissible
tail (C++26 paren aggregate init fills the omitted tail from NSDMIs — the
Aggregate branches are `if constexpr` so the field-array indexing never
instantiates for a fieldless type, the array<info,0> trap), luacats marks the
suffix `?`. An NSDMI before a required field stays required (no parameter gaps).
`aggregate_defaults_from` is the stricter value-extracting form: whole-T must be
default-constructible, shrinks past non-copy-constructible fields. A default
whose type needs registration (welded class/enum instance) has no
expression-shaped repr, so both Python rods spell it `...` in the signature
(pybind11 `arg_v` descr / nanobind `arg::sig`) while keeping the runtime value —
otherwise pybind11-stubgen hard-fails on `<X object at …>`. CAVEAT: those
defaults convert EAGERLY at registration — a welded default type must register
before the aggregate does; the module walk is declaration-ordered, so an
umbrella that pre-opens a submodule namespace (for its doc annotation) ahead of
the core types moves that submodule to the walk's front and imports die with
std::bad_cast (guide has a warning box). Const members keep a
struct an aggregate: read-only fields + the field ctor still brace-inits (the
immutable-settings pattern). Locked by core/aggregate_defaults.cpp
(compile.aggregate_defaults) + methods.hpp `Window`/`Frozen` ↔ test_methods.py /
methods_spec.lua + the stub golden's `Splash` and `?`-marked suffixes.
Constructors resolve SYMMETRICALLY (policy + per-ctor marks — opt_in binds only
marked-include ctors; bind_traits `ctor_group<R,Type,L,Pol>`), with two
fail-safes: the DEFAULT ctor is exempt from opt_in's default-out (an implicit one
has no declaration to mark; explicit marks on a declared `T() = default` ARE
honored — `default_ctor_admitted`), and the carriage's no-constructor-left
static_assert hard-errors when filtering leaves a type with no ctor at all UNLESS
the same resolution under `automatic` would also yield none (mark::exclude-ing
every ctor = the explicit factory-only escape; a custom resolution pruning under
any policy doesn't false-fire). Locked by overloads.hpp `OptInCtor`/`FactoryOnly`/
`NoDefault` + negcompile.optin_uninstantiable. Function / method / constructor **parameter
names** reach Python as keyword arguments (`py::arg`) when every parameter of that
signature is named.

**Copy & move ctors:** the COPY ctor never binds as an init overload — it is the
`Copyable` bool of `add_constructors` (carriage: `is_copy_constructible_v<T>` &&
bind_traits `copy_ctor_admitted<R,Type,L>`); the Python rods turn it into the
SUBCLASS-FAITHFUL copy protocol `__copy__`/`__deepcopy__(memo)` **only** — never
a `T(other)` init overload (that C++-ism is unpythonic — copying goes through the
`copy` module — and would clash with a one-arg user ctor; so a lone Grabby-style
greedy ctor now serves `T(other)`, and copying.hpp `Grabby` /
`test_copy_protocol_is_independent_of_constructors` lock exactly that: the
protocol still copies faithfully because it never routes through Python ctor
overload resolution). `_copy_instance<T>`: `type(self).__new__` shell → the C++
payload copy-constructed *in place* on it (NOT via `__init__`) — a subclass shell
(`Py_TYPE(out) != tinfo->type` on pybind11 / `nb_inst_python_derived` on nanobind)
gets `construction_type<T>` (the trampoline), a plain instance gets `T`; pybind11
sets `v_h.value_ptr()` then `tinfo->init_instance(inst, nullptr)` (the exact
finalize pybind11's own ctor path runs, pybind11.h:~1237), nanobind placement-news
into `inst_ptr<void>(out)` then `inst_mark_ready` (every nanobind instance is
alias-sized, so either payload fits) → `__dict__` carried over, deepcopy'd through
the memo with `memo[id(self)]` recorded FIRST so cycles terminate → `__slots__`
state carried too, names via `copyreg._slotnames` (the stdlib's own MRO walk, what
pickle uses); memo typed `object` not
`dict` — bare `dict` fails strict-mypy stubcheck. A Python subclass thus copies
as itself with overrides dispatching. The trampoline interplay: the backend
builds the ALIAS payload on a subclass shell only if the trampoline is
constructible from `const T&` — `WELDER_PY_TRAMPOLINE(TRAMP, BASE)` (macro now
takes the trampoline's own name) declares a guarded template copy-from-base ctor
(+ `TRAMP() = default`; SFINAE'd away for noncopyable bases; the generated
trampolines rod emits the same macro), and the rods static_assert
`is_constructible_v<construction_type<T>, const T&>` in the Copyable block so a
hand-rolled trampoline without it is a designed compile error (else the copy
would silently hold a plain-base payload and stop dispatching). The
Lua/luacats/trampoline rods ignore Copyable (no Lua copy protocol — same bucket
as doc/return_policy).
Admission mirrors `default_ctor_admitted` exactly: implicit copy ctor → admitted
iff copy-constructible (opt_in default-out does NOT apply); declared → its
explicit marks honored under an `automatic` baseline (exclude / exclude(lang)
suppresses; deleted/private copy = no protocol, no error). MOVE ctors never bind
anywhere: skipped structurally, `exclude` a no-op, but an `include`/`only` mark
on one is a designed hard error (bind_traits `validate_move_ctor_marks`, run by
bind_class_interior for every welded/nested class → throws
`diag::marked_move_constructor` naming __copy__ as what crosses). Cases:
`tests/common/cpp/copying.hpp` + test_copying.py (Python-only inclusion, like
doc.hpp; subclass fidelity, memo/cycles, trampoline dispatch-on-copy, tack) +
negcompile.move_ctor_marked. Mechanism probed standalone first (pybind11 +
nanobind, scratchpad copyprobe) per house convention.

**Member resolution marks:** `exclude`/`include` plus `mark::only(lang...)` — the
closed-world mark: the COMPLETE set of languages the member binds for; under
`opt_in` it doubles as the opt-in; `exclude` still beats it; repeats union; bare
form diagnosed at resolution (reflect.hpp `member_bound`, throws
`diag::bare_mark_only`). Cases: `resolution.hpp` `only_py` /
`only_then_excl` / `only_lua` + test_resolution.py / resolution_spec.lua.

**Marks resolve PER OVERLOAD (ctors included), via the resolution.** Class members
gate on `Resolution::class_member_participates(mem, L, pol)` (= member_bound for
the shipped resolutions), and the CARRIAGE computes each name's participating
overload group (bind_traits `{method,operator,function}_overload_set<Resolution>`)
before calling the rod's GROUP hooks (`add_method<Fns,Style>` etc., array-NTTP) —
rods never re-derive membership, so per-overload marks and bespoke signature-level
resolutions bind identically on every rod (incl. the Lua one-value-per-name
tables). Cases: `tests/common/cpp/overloads.hpp` + test_overloads.py /
overloads_spec.lua (a per-language-excluded overload, an everywhere-excluded one,
an excluded ctor, OptInCtor). weld_function<Fn> is group-aware too: it welds Fn
PLUS Fn's participating same-name siblings (Fn first — it names the group; an
identifier-less substitute()d Fn stays alone), keeping the semi-manual route
consistent with the namespace walk.

**Returned handles (the mixing story):** every `weld_*` forwards its rod hook's
return — `weld_type` → the class/enum handle (`py::class_<T>` / `nb::class_<T>` /
`sol::usertype<T>` / luabridge `class_handle`), `weld_function` → the bound
function object on pybind11/nanobind (`m.attr(name)`) and sol2 (the table
entry), void on luabridge/text rods;
`weld_namespace_as_submodule` → the submodule handle; `weld_variable` forwards
but all shipped rods return void. Carriage bind_function/bind_variable forward
(`if constexpr is_void` dance around the session in bind_variable). Cases:
`chaining.hpp` (+ per-backend `WELDER_TEST_CHAIN_*` seams) + test_chaining.py /
chaining_spec.lua.

## Protected members — `policy::weld_protected`
Access admission is a layer BEFORE `member_bound`: bind_traits
`member_access_admitted<Resolution>(mem, L, bound_into)` — public always in;
PRIVATE hard-out before any hook (design invariant, no resolution can readmit);
protected via the resolution's OPTIONAL
`protected_participates(mem, L, bound_into)` hook (requires-detected; a
leftover 2-arg hook hard-errors via a thrown `diag::stale_hook_signature`
rather than being silently ignored), falling back to the declaring class's
`policy::weld_protected` annotation (`reflect.hpp` `protected_welded` — masked
like exclude/include, bare = all langs, repeats union, read through template
instantiations from the template; parent_of(mem) = the instantiation, NOT the
alias). Shape predicates `is_method_candidate`/`is_operator_candidate` now
reject only private (`!is_private`); the admission is applied at the 3
carriage bind_members sites AND in bind_traits `{method,operator}_overload_set`
(else a protected overload leaks into a public sibling's group).
`marker_resolution` spells the annotation hook explicitly;
`greedy_resolution` is now `template <bool WeldProtected = false>` — the
whole-pass knob for unannotatable third-party libs
(`basic_carriage<greedy_resolution<true>>`; `tack_welding_carriage` =
`greedy_resolution<>`, public-only default; knob ORs with the annotation).
CONSTRUCTORS deliberately exempt (is_bindable_constructor/default_ctor_admitted
keep is_public): no PM for a ctor; a protected DEFAULT ctor still works through
a trampoline's construction_type; non-default protected ctors = future
trampoline-rod forwarding ctors. Trampoline interplay: protected virtuals were
already slots; with weld_protected they ALSO bind, so Python can call+override
(pybind get_override shadowing works — the bound method and the override share
the name).

**Emission (the gcc-16 field workaround):** methods/operators/statics bind via
the usual `&[:Fn:]` (fine for protected). Protected DATA members can't — gcc-16
wrongly access-checks a *dependent* `&[:Mem:]` on protected data (non-dependent
passes, dependent PMF passes, `extract<F C::*>` also checked; see the
toolchain context). Workaround isolated in bind_traits
`detail::field_access<Mem>` (get/set over the UNCHECKED member-access splice
`c.[:Mem:]`); each rod's add_field takes a property path for `!is_public(Mem)`
only — pybind `def_property(_readonly)` / nanobind `def_prop_r{o,w}` (both
reference_internal, matching def_readwrite) / sol2 `sol::property` /
LuaBridge3 `addProperty(get[, set])`. Fold back to `&[:Mem:]` when gcc fixes
the check.

**bound_into (resolution-hook context):** every per-member resolution hook
takes a trailing `std::meta::info bound_into` — the entity whose binding
RECEIVES the member: the welded type for class members (bind_members gained a
`BoundInto` NTTP held fixed through the flattening recursion, so it ≠
`parent_of(mem)` exactly for a flattened base's member), the swept namespace
for member/alias hooks, the parent namespace for namespace_participates, the
walked type for is_native_base, ^^E for enumerators, Type for ctor machinery.
`participates` + `counts_as_registered` deliberately have none (manual entry
points / pure registration predicate). The overload-set selector signature is
now `(info, lang, info)` (overload_group gained a BoundInto NTTP). Shipped
resolutions ignore it (unnamed param); it exists for bespoke hooks ("admit this
mixin's protected members only into Derived").

Tests: resolution.hpp `Shielded` (methods/overloads/static/data/exclude/private)
+ `ShieldedPy` (lang-scoped: py yes, lua no) + `OptInShielded` (include still
required) ↔ test_resolution.py / resolution_spec.lua; templates.hpp `Vault`/
`IntVault` (annotation through the alias route) ↔ test_templates.py /
templates_spec.lua; gen_trampolines.hpp `Keep` (bound+overridable protected NVI
hook, protected data; `Golem::secret` = the unbound negative control) ↔
test_gen_trampolines.py; namespace.hpp `foreign_protected::Panel` tacked with
`greedy_resolution<true>` + `foreign_mixed::Meter/Display` tacked with a
bound_into-keyed hook (the flattened protected member appears on Display, not
on Meter — locks the BoundInto threading) ↔ test_namespace.py /
namespace_spec.lua; compile lock tests/core/protected_access.cpp (reader masks,
hook-vs-fallback, bound_into-keyed hook, private hard-out even under an
admit-everything hook, dealias-required-for-alias note). NB
detail::aggregate_fields gained the n!=0 fill guard (std::array<info,0>::
operator[] is not consteval — a FIELDLESS class hard-errored the Lua rods'
ctor machinery; Meter was the first such class in the suite).

## Method-backed properties — `getter` / `setter`
`[[=welder::getter]]` (const, 0-param, non-void, non-&&-qualified) /
`[[=welder::setter]]` (exactly 1 param) member functions bind as ONE idiomatic
property instead of methods, for the languages the mark covers (bare = all;
callable `getter([lang…,] "name")` — the weld_as arg grammar with the name
OPTIONAL). Stored form `detail::accessor_spec` is deliberately NON-templated
(fixed 64-char inline name buffer, `accessor_name_capacity`): the resolution
machinery reads it off DYNAMIC reflections (member_bound, overload-set
selectors) where a length-templated spec can't be extracted. Readers
(reflect.hpp): `accessor_marked` / `is_accessor_for` / `has_accessor_mark` /
`accessor_explicit_name`.
**Resolution:** the accessor mark IMPLIES the opt-in under `policy::opt_in`
(member_bound's opt_in branch ORs `is_accessor_for`; scoped marks imply only
for covered langs); exclude still beats it. The method sweep SKIPS accessors
symmetrically (bind_members' method loop + `method_overload_set` both filter
`!is_accessor_for(m, L)`), so for uncovered languages the function binds as an
ordinary method (the per-language degradation).
**Naming/pairing** (naming.hpp `detect_case`/`accessor_property_words`/
`strip_accessor_word` + bind_traits `property_key`/`property_bound_name`):
explicit mark name = verbatim (never styled; `weld_as` on an accessor is
diagnosed — the mark IS the rename tool); else derive by stripping a leading
`get`/`set` WORD (≥2 words only; `is_` never stripped) — the derived name is
`Style::transform_field(Getter)` FIRST, then stripped in the styled spelling's
own detected convention (keeps the info-taking hook contract; commutes with
word-preserving styles). PAIRING keys on the case-normalized word list, so
get_x/setX/SetX/overload-style all pair, mixed conventions included; the
getter's spelling is authoritative.
**Machinery** (bind_traits "method-backed properties" section):
`collect_accessors` (own + flattened non-native bases, mirrors
collect_member_operators) → `property_entries<Resolution>(type, L)` (validate →
pair; getters decl-order fix emission order) → carriage `bind_properties`
(bind_class_interior, after bind_members) → rod hook
`add_property<T, Getter, Setter>(cls, name)` — Setter = null info{} =
read-only; NAME arrives driver-resolved (the driver owns property naming, like
class/enum names). Gate: getter via assert_callable_bindable; setter via NEW
`assert_setter_bindable` (bindable.hpp) — PARAMETER only, the return is
discarded by every rod (a fluent `T& set_x()` chains in C++, writes plainly
outside), so pybind11/nanobind/sol2 wrap a non-void setter in a void lambda
(LuaBridge3 wraps via callables) — binding the member pointer directly would
convert an ungated value.
**Diagnostics** (diag.hpp, all thrown in property_entries unless noted):
malformed_getter / malformed_setter / accessor_role_conflict (both marks, one
fn, one lang) / duplicate_property_accessor / setter_without_getter (no
write-only properties) / accessor_weld_as_conflict / static_property_accessor
(deferred — def_property_static exists, Lua has no surface) /
virtual_property_accessor (a property under the method name breaks by-name
Python override dispatch — rejected, not subtly wrong) /
property_name_collision (vs bound fields + non-accessor methods, WORD-KEY
comparison — deliberately convention-insensitive; nested types exempt —
PascalCase type vs camel property is legit) / accessor_name_too_long (factory).
Free-function accessor marks: static_assert in the namespace walk's function
branch + bind_function. Alias admissibility lists reject accessor_spec too.
**Rods:** pybind11 def_property(_readonly) (unannotated → framework default
reference_internal, rvalue-forced to move); nanobind def_prop_rw/ro (nanobind
does NOT default prop getters to reference_internal → rod passes it explicitly
for pointer/lvalue-ref returns, `automatic` for values — an explicit reference
policy on a value would dangle); sol2 `sol::property`/`readonly_property`;
LuaBridge3 `addProperty` over pms held as `GF T::*` (base→derived pm
conversion, the add_field maneuver); luacats `---@field name <ret-type>` +
`(read-only)` note (getter doc = description); trampolines no-op. Getter doc =
property __doc__ (doc_of, not function_docstring); return_policy on the getter
honored by the Python rods.
Tests: tests/common/cpp/properties.hpp ↔ test_properties.py /
properties_spec.lua (all four runtime rods; Circle overload-style +
read-only + control method, Vehicle camel/mixed/is_-predicate, Gauge explicit
names + py-scoped marks + lua-excluded setter, Padlock opt_in implication, Tag
flattened-base + fluent setter, Sealed weld_protected); compile lock
tests/core/properties.cpp (detect_case, strip, pairing, per-style
property_bound_name, member_bound implication); neg
property_{setter_without_getter,nonconst_getter,virtual_accessor,
weld_as_on_accessor,shadows_member,free_function_accessor}; luacats golden
`stubdemo.Throttle`; rod_probe + user_lang probe rods carry the hook.

## Operators (member + free + spaceship synthesis + stringifier)
An operator binds under its dunder/metamethod (`operator+` → `__add__`/`__add`,
…), unary vs binary told apart by arity (`is_unary_operator` — member: 0/1
params; free/static: 1/2). **Emission is one combined SLOT GROUP per (operator,
arity) per welded type** (carriage `bind_operators`, run once per type after
`bind_members` — NOT part of the flattening walk): entries = T's member
operators + flattened non-native bases' + the **anchored free operators** of
T's nearest enclosing namespace (`operator_entries`/`operator_slot_set` in
bind_traits; anchor = a param decays to exactly T, rvalue-ref operand
disqualifies; base-anchored free ops ride class inheritance instead). A free
operator resolves like a member of its anchor (T's policy + own marks,
`class_member_participates`, bound_into = ^^T); the namespace walk's function
branch now SKIPS operator functions (they'd have no module name anyway). Rod
hook: `add_operator<T, Fns>` (T leads; group may mix member/free). Python rods:
per-entry — reflected free entries (T on the RIGHT, `free_operator_reflected`)
bind the reflected dunder (`reflected_dunder` in python/operators.hpp:
`__rmul__`… ; comparisons mirror: free `<(int,T)` → `__gt__`) via an
operand-swapping wrapper; all binary arith/cmp defs pass
`py::is_operator`/`nb::is_operator` (`dunder_uses_not_implemented`) so failed
conversions yield NotImplemented (reflected protocol works); operator defs
never pass py::arg names (`_def_operator` — dunders are positional-only, and
pybind rejects annotations on a 2-arg free fn def'd as a method). Lua rods: one
registration per slot (sol2 `sol::overload`; LuaBridge3 typed `addFunction`
when every entry is direct, else the WHOLE group becomes one raw lua_CFunction
dispatcher `_op_dispatch` — LuaBridge3's typed path statically requires T as
first param AND splits const/non-const into disjoint shadowing overload sets).
sol2's automagic operator enrollments (to_string/call/eq/lt/le) are now OFF
(_make_usertype: they bypassed marks — caught by PyOnlyCmp); bases now assigned
post-creation (`ut[sol::base_classes]`), default ctor off in enrollments
(replaces sol::no_constructor).
**`operator<=>` never binds directly** (std::*_ordering doesn't cross): the
slot group routes to `add_comparisons<T, Fns, Covered>` which synthesizes
REWRITTEN EXPRESSIONS (`welder::detail::synthesized_comparison<A,B,cmp_slot>` —
plain `a < b`, so C++ rewriting picks the overload; heterogeneous + reversed
included). Python: 4 slots (`synthesize_comparisons` in operators.hpp); Lua:
`__lt`/`__le` only (both operand orders for hetero; LuaBridge hetero → raw
`_cmp_dispatch`). `Covered` = `covered_comparison_slots` (explicit participating
`< <= > >=` beat synthesis per slot; also keeps Lua one-writer-per-slot). `==`
NEVER synthesized — C++ only rewrites `==` from `operator==`; a DEFAULTED <=>
implicitly declares one and members_of enumerates it (probed), so it binds via
the normal path. Gate: `assert_operands_bindable` (operand types only, never
the ordering return). Lua semantic seam: `a > b` IS `b < a`, so an explicit
inverted `<` makes Lua's `>` mirror it while Python synthesizes `>` from <=>
(locked in operators_spec).
**Stringifier**: free `operator<<(std::ostream&, T)` (`is_stringifier_for`,
excluded from slot groups, ostream param exempt from the gate) →
`add_stringifier<T, Fn>` → Python `__str__` / Lua `__tostring` via shared
`welder::detail::stringify<T, Fn>`; luacats/trampolines no-op. Entry `[0]` only.
**Hidden friends are invisible to P2996** (not in members_of(class) nor the
namespace — probed): document-only limitation (move to namespace scope or bind
by hand). luacats: free-left entries emit `---@operator` with operand =
param[1]; reflected entries dropped (self is left in LuaCATS); <=>/tostring
emit nothing (Impulse in the stub_gen corpus locks this).
The operator→name map stays the rod's `special_method_name(op_fn)` (nullptr =
not exposed, gates slot eligibility; spaceship branches BEFORE the map).
In-place compound assignment (`operator+=`), `&&`, `||`, `++`, `--`,
`operator=` remain unmapped. Cases: tests/common/cpp/operators.hpp (Coin free +
excluded free −, Mixed member+free one slot, Scaled reflected + inserter,
Version defaulted <=>, Temp custom <=> no ==, Account hetero <=>(int)+==(int),
Ordered explicit-beats-synthesis, PyOnlyCmp exclude(lua) on <=>) +
test_operators.py / operators_spec.lua.

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
finalizes. The enum `doc` becomes the Python docstring. **Per-enumerator docs** —
an enumerator has no per-member docstring slot the Python stub tools surface — are
folded into the enum's **class** docstring as an *Attributes* section (the one
place `pybind11-stubgen`/nanobind's stubgen carries them into the `.pyi`): the
carriage's `collect_enum_docs<B,E,Style>()` gathers the participation-filtered,
styled `{name, doc}` pairs (mirroring `emit_enumerators`) into a
`welder::detail::enum_doc`, and the Python rod's `make_enum` renders it via
`DocStyle::format_enum` (Google `Attributes:`, NumPy underlined, Sphinx `:var:` —
locked by `tests/core/doc_styles.cpp`). The doc-folding rods take the extended
`make_enum(m, name, const enum_doc&)`; the Lua/luacats rods keep the legacy
summary-only `const char* doc` form, and the carriage's `make_enum_of` /
`make_nested_enum_of` pick whichever is present. (The **luacats** rod surfaces
per-enumerator docs its own way — `add_enumerator` emits each enumerator's `doc`
as a nested `--- ` comment above its `Name = <int>` entry in the `---@enum` table;
LuaLS attaches that to the member for hover/completion, verified warning-clean. The
sol2/LuaBridge3 runtimes have no docstring slot and drop it.) An
enum-typed member/parameter binds because the enum is welded (bind the enum first,
like a welded base). Tested: `tests/common/cpp/enums.hpp` + `tests/python/test_enums.py`;
per-enumerator docs in `tests/common/cpp/doc.hpp` (`Channel`) + `tests/python/test_doc.py`.

## Nested types (member classes + enums)
A type declared INSIDE a welded class resolves like any other class member —
the OUTER's policy + the nested type's own exclude/include/only marks
(`class_member_participates`), with the usual access admission
(`member_access_admitted`: private never; protected under `weld_protected`) —
never via a `weld` of its own (nested types are interface helpers of the
enclosing type; the enclosing weld is the discovery marker). Carriage:
`bind_type` runs the shared `bind_class_interior` (nested walk FIRST — pybind11
converts a later ctor/method *default argument* at registration time, so a
nested enum must already exist — then ctors, then members); `bind_nested_types`
walks `members_of(Outer)` for non-alias, named, complete class/enum member
types and recurses via `bind_nested_type`/`bind_nested_enum` (which share the
interior/enumerator machinery with `bind_type`/`bind_enum` — no participates
assert, participation was the member resolution). Skipped deliberately: member
type ALIASES (alias-welding is namespace-scope only), unions, unnamed types,
incomplete member types, and a FLATTENED BASE's nested types (a nested type
registers exactly once, with its declaring class — two deriveds flattening one
mixin would double-register; a flattened signature naming one fails the gate
until the base is welded).

**Rod primitives (optional, requires-detected):** `make_nested_class<T, Bases>(m,
outer_cls, name, doc, iseq)` / `make_nested_enum<E>(m, outer_cls, name, doc)` —
place the type under the outer's binding; a rod without them falls back to flat
module-scope `make_class`/`make_enum` (collision caveat documented). A third
hook `finish_nested_class<T>(m, outer, cls, name)` runs AFTER the interior
(innermost-first) for rods whose class handle re-opens by name/path. Shipped:
pybind11/nanobind pass the outer class handle as the registration scope
(`module.Outer.Inner`, nested `__qualname__`; `_make_class` generalized to a
`py::handle`/`nb::handle` scope, the trampoline weaving shared via
`_make_class_at`; `enum_handle.scope` is `py::object`); sol2 registers through
the module then moves the usertype onto the outer's table (`outer[name] = ut`,
temp module key nil'd — a lua reference value is a static entry; the enum
mirror closure uses `sol::var` for the plain integer); LuaBridge3 registers
under a temporary DOTTED module key ("Outer.Inner", re-open-by-path safe) and
`finish_nested_class` raw-moves the class table onto the outer's static table
(raw entries win before LuaBridge3's `__index` guard) — nested enums are raw
value tables written via the C API, unscoped ones mirroring onto the CLASS
table; luacats emits `---@class mod.Outer.Inner` blocks into the outer's
`trailing` buffer (flushed after the outer's declaration; writers gained a
`sink` redirect; `qualified_name` now walks class scopes); trampolines rod uses
the flat fallback (its `cpp_qualified_name` already spells `::ns::Outer::Inner`).

**Gate:** both resolutions' `counts_as_registered` mirror the sweep exactly via
`detail::nested_type_registered` — see bindability-gate.md. An unscoped nested
enum exports onto the CLASS (Robot.quiet), mirroring C++, on every rod.
`weld_as`/`doc` on nested types ride the normal name/doc paths. A nested type
with its own `weld` + `mark::exclude` = the manual flat-registration escape
(`weld_type<Outer::Inner>(m, "name")` still works; without the exclude it would
double-register).

**Member type aliases (the class-scope alias route):** a member alias
participates iff its target FAILS the bindability gate — registering exactly
the types that otherwise couldn't cross the boundary, nested under the outer,
named by the alias (alias `weld_as` → target's `weld_as` → styled identifier;
`detail::alias_bound_name` grew an ent_kind param). Gate-passing targets
(castable, bindable STL wrapper, welded, registered) are SKIPPED — so
`value_type`/`iterator` conventions cost nothing, an alias to a welded or
sibling-nested type never double-registers, and under greedy resolution
(everything complete passes) member aliases never participate in a tack weld,
by construction. The alias's exclude/include/only marks + the outer's policy
apply (member rules); other marks are diagnosed (`member_alias_marks_admissible`,
reflect.hpp). exclude on a DECLARED nested type + an alias = the class-scope
RENAME escape (the exclude fails the gate, the alias re-registers). Duplicates
within one class are diagnosed (`detail::sole_member_alias_of_target`);
cross-class duplicates of one unwelded target are user-managed (import-time
framework error). Carriage: an alias branch in `bind_nested_types`
(participation → `!welder::bindable<B, Target, L, Resolution>()`, so
participation is ROD-dependent — precedent: operator eligibility);
`bind_nested_type`/`bind_nested_enum` gained a name override (via `name_of_or` —
the consteval fallback must stay lazy for identifier-less specializations) and
`bind_nested_type` a Decl NTTP threaded to the rods (luacats' extended
`make_nested_class<T, Decl, Bases>` records rename keys as
`has_identifier(^^T) ? qualified_name(^^T) : qualified_name(Decl)` — target key
when nameable so REFERENCES remap, alias key for unnameable specializations;
the trampolines rod's flat fallback receives the alias Decl and spells
`::ns::Outer::AliasName`).

**The SCOPE-AWARE oracle** (`detail::scoped_registration<Resolution, Scope>` :
Resolution, carriage.hpp): an alias is unrecoverable from the type it names, so
the plain oracle can't see alias registrations; the carriage gates a class's
members (fields/methods/operators/ctors — bind_members' `Reg` + the
bind_class_interior ctor asserts) through this wrapper, which additionally
counts (a) types a participating member alias of Scope registers
(`detail::registered_by_member_alias` — deliberately NO gate re-check: the
oracle leaf is reached only after every other bindable() branch failed, which
IS the sweep's arbiter) and (b) the nested chain re-run alias-aware (an alias
target's own nested types recurse). Cross-class use of an alias-registered
type stays trust_bindable territory (locked by
negcompile.member_alias_cross_class — Meter's scope is blind to Panel's
alias). Tack never sweeps member aliases (foreign::Widget::Twin absent ↔ both
namespace specs). TRAMPOLINE-GENERATOR caveat: the
trampolines rod's permissive has_native_caster makes every alias target pass
its gate, so the generator never sees member aliases — an alias target with
virtuals needs a HAND-WRITTEN trampoline (spelled through the alias) or
bind_flat; the Python rods' make_class assert catches it at compile time.
ENUM-ORACLE FIX that fell out: both Python rods' `_needs_registration` now
force `is_enum_v` (pybind11-3/nanobind enum casters don't derive the base
caster, so enums read "native" — the gate never actually required a welded
enum on the Python rods; masked while every suite enum was independently
welded). Member-alias tests: nested.hpp `Console` (+ `nested_vendor`) ↔
test_nested.py / nested_spec.lua; core locks in tests/core/nested_types.cpp
(`Desk`/`ntv` — scoped vs plain oracle, per-scope blindness, excluded-alias);
neg member_alias_{duplicate,forbidden_mark}; luacats golden `Gauge.Face`
(reference remap).

**Alias-welded instantiations:** a specialization's nested types bind too —
under the ALIAS's name (`IntSilo.Hatch`): the sweep reads them off the
instantiation, and (weld-on-template) the gate's welded_for reads the template's
weld through it, so signatures using them pass. For an ALIAS-OPT-IN template
(weld only on the alias) the nested type still REGISTERS (member rules need no
weld), but a signature naming it — or the instantiation itself — fails the
stitch gate: the oracle is a pure predicate of the declaration and cannot see a
namespace-scope alias's weld (blind spot, trust_bindable territory) — BOTH
directions locked: negcompile.alias_optin_in_signature (the gate fires) and
templates.hpp `Pack::twin` + the type-level `trust_bindable<Pack<int>>`
specialization (the hatch clears it, and the returned instance converts
through the alias registration; test_templates.py / templates_spec.lua). Text-rod spelling caveat: a nested VIRTUAL type of a
specialization can't be respelled by the trampoline generator
(`cpp_qualified_name` truncates at the unnameable `Box<int>` segment) — now
DIAGNOSED, not silent: `cpp_spellable` (trampolines document.hpp) hard-errors
in `document::add` pointing at a hand-written trampoline through the alias /
bind_flat; locked by negcompile.nested_virtual_in_specialization.

Tests: `tests/common/cpp/nested.hpp` ↔ test_nested.py / nested_spec.lua (all
four runtime rods; Robot/Machine/Panel/Cabinet/Rig — marks, deep nesting,
opt_in, weld_protected, private; Robot::Beacon = the exclude+weld manual-flat
escape exercised at runtime via weld_type<Robot::Beacon>(sub, "RobotBeacon");
Robot::Probe (fwd-decl) + Robot::Blob (union) lock the silent sweep skips); alias-welded: templates.hpp `Silo`/`IntSilo`
(+ `vendor_tpl::Pack::Lid`, the opt-in registration-only case) ↔
test_templates.py / templates_spec.lua; greedy: `foreign::Widget::Stat` in
namespace.hpp ↔ both namespace specs; trampolines: `Tower::Bell` in
gen_trampolines.hpp ↔ test_gen_trampolines.py; luacats golden
(`stubdemo.Gauge`); compile lock tests/core/nested_types.cpp; neg
tests/python/pybind11/cpp/neg/nested_excluded_in_signature.cpp. Stub note: the
pybind11-stubgen native-enum relaxation in tests/python/pyproject.toml covers
`*.nested`/`*.templates` too.

## Unions — never bind
Categorical, designed rejection (reading an inactive member is UB; C++ has no
runtime active-member query, so no safe accessor can be generated). Hard errors
at every entry: the gate (union-specific message; a `weld` on the union never
vouches), `bind_type` (weld_type on a union), and the namespace walk (a
weld-MARKED union; unmarked = skipped). ANONYMOUS union members + unnamed
bit-fields: `bind_members` skips unnamed data members structurally, and an
unnamed field disables the synthesized aggregate ctor. Blessed path:
std::variant (all four rods, value conversion; matching order caveat —
Lua rods single-pass declaration order, Python rods two-pass exact-first —
detailed in bindability-gate.md). Full detail + escape hatches:
`.claude/context/bindability-gate.md` "Unions: categorically rejected"; guide:
bindability.md "Unions never bind". Tests: `tests/common/cpp/unions.hpp` ↔
test_unions.py / unions_spec.lua; neg union_weld_type / union_member /
union_welded_in_namespace.

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
runs through the derived type's own trampoline, not the base's). Slots dedup by
**vtable identity** (`detail::same_slot`: name + parameter types + cv/ref quals —
NOT full `type_of`: return type excluded so a **covariant** override is ONE slot
kept with the narrowed most-derived signature, and `noexcept` excluded so a
strengthening override folds), keeping the most-derived declaration; that decl's
`bind_flat`/access governs. The walk uses `access_context::unchecked()` (like the
rest of the core): **protected** virtuals (NVI hooks) ARE slots — overridable via
plain attribute lookup though never *bound* (the carriage's `is_public` filter is
about binding, not overriding); **private** declarations are filtered out on output
(base fallback couldn't name them), and a private redeclaration *withdraws* an
inherited slot (it claims the slot in dedup, then filters). Regression:
`Bird : Animal` in `overridable.hpp` (inherits speak/legs, adds fly) with
`test_derived_class_overrides_{inherited,own}_virtual`; slot semantics locked
compile-only in `tests/core/trampoline_slots.cpp` (covariant/overload/access/
noexcept/C-variadic render).

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

**Dispatch:** `WELDER_PY_OVERRIDE(fn, args…)` → `WELDER_PY_OVERRIDE_AS(^^welder_py_base::fn, fn, args…)`
→ each backend's `override_dispatch<(SLOT)>` (name/return-type/pure-ness from the slot
reflection). The `_AS` form exists because `^^Base::fn` is **ill-formed for an
overloaded virtual** (no overload-set reflection in P2996; gcc-16: "cannot take the
reflection of an overload set") — for overloads, hand-written trampolines pass an
explicit slot via `welder::rods::python::virtual_slot(^^T, "fn", ^^ret(args) quals)`
(searches `overridable_virtuals`, so inherited slots too; no match = const-eval error
naming a diagnostic function; extra parens keep commas out of macro splitting), while
the textual `fn` arg only spells the qualified base fallback (overload resolution
picks the overload from the forwarded args). Generated trampolines emit `_AS` with
`overridable_virtuals(^^T)[k]` for every override, so overloads Just Work there.
Runtime semantics: both backends look the Python override up **by name**, so all C++
overloads of a name dispatch into the ONE Python method.
pybind11's `get_override(const T*, name)` keys the Python-object lookup off `typeid(T)`
— the *static* pointer type — so the macro casts `*this` to `welder_py_base` (the
**registered** welded type, `class_<T, Trampoline, …>`), **not** the virtual's declaring
class. For an inherited virtual those differ; casting to the declaring base looks the
instance up under the wrong registration and silently misses the override (C++ sees the
base impl). nanobind is unaffected — it dispatches through its own trampoline storage +
`detail::ticket`, not `typeid`. Base fallback is a **textually
qualified** `welder_py_base::fn(args)` lambda — NOT `self.[:Fn:]()`, which splices to
a *virtual* call and infinitely recurses. `WELDER_PY_TRAMPOLINE(Tramp, Base)` injects the
`welder_py_base` alias + inherited ctors, plus (nanobind only) a
`nb::detail::trampoline<slot_count>` storage member; pybind11 uses `get_override` +
`detail::cast_safe`, no storage. **Reference** returns are `static_assert`-rejected
(lifetime); **pointer** returns work (Python override returns an instance or None →
T*/nullptr — the covariant tests use this with `return_policy(rv::reference)`).
Macros are neutrally named so one trampoline source compiles under either
Python rod.

**Generating trampolines (`welder::rods::trampolines::rod`).** The hand-written
trampoline is mechanical, so a build-time text-emitting rod emits it — the Python
analogue of the LuaCATS stub rod, over the same driver. Files:
`src/welder/rods/python/trampolines/{document,rod,module}.hpp`; CMake helper
`cmake/WelderTrampolines.cmake` (`welder_generate_trampolines()`); target
`welder::trampolines`. Only `make_class<T>` emits (a `struct … : T { WELDER_PY_TRAMPOLINE(PyT, T);
one WELDER_PY_OVERRIDE_AS per overridable virtual };` + a `trampoline_for<T>` spec), skipping
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
+ member marks then resolve. Binds classes (`weld_type<T>`), **alias-declared
template instantiations** (see the alias bullet below), free functions (overloads
included), and namespace variables as module attributes — a **value snapshot if
const/constexpr, else a live get/set property** over the C++ global (via a
`ModuleType` `__class__` swap). A **nested namespace** resolves under the
*parent's* policy (no weld; automatic recurses unless excluded, opt_in only if
included — keeps `detail`/`impl` out) and becomes a submodule when it holds bound
content. Declaration order.

## Whole-module binding — `weld_module<^^ns>(m, pre, post)`
Fills an *existing* module (pre hook → `weld_namespace` → post hook; namespace
`doc` → module doc). The C entry symbol `PyInit_<name>` must be preprocessor-pasted,
so the rod-agnostic `WELDER_MODULE(ns, rod[, WelderType])` macro (`module.hpp`)
wraps it (namespace token = module name, optional trailing `{ }` post-glue with the
module handle in scope as `module`). The optional third argument is the exact
`welder::welder<…>` to drive the weld — the way to thread a name style / custom
carriage through the one-line form (variadic, so template-id commas survive;
`detail::module_welder_t` picks override-else-default; each rod entry macro
static_asserts the override's `module_type`). Covered by cookbook recipe 07 (all
three runtime entry points use the styled form). One `WELDER_MODULE` per rod per
TU; two Python rods collide (both emit `PyInit_<name>`).

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

**Member function templates:** skipped silently by the member walk
(`is_function`==false; is_method_candidate never matches) — marks on them are
silently inert too, and NOT diagnosable: annotations_of on an uninstantiated
template throws (P2996 restriction; tried 2026-07-15 — a binding_marked
static_assert in bind_members blew up on UNMARKED member templates because the
query itself throws; reverted). No weld entry
exists; the route is CHAINING on weld_type's returned handle
(`cls.def("mix", &T::mix<double>)`), and on the Python rods the chained
instantiation JOINS the welder-bound non-template overload group (pybind/nanobind
merge same-named defs; exact-match-first so order can't shadow) — Lua frameworks
REPLACE same-key registrations, so the pattern is Python-only. When the template
shares its name with non-template overloads, `^^name` is an overload set →
substitute() cannot form instantiations either (the weld_function route is closed;
plain `&T::f<double>` disambiguates fine). Locked by chaining.hpp Mixer +
chaining_tpl_fns::blend ↔ test_chaining.py (WELDER_TEST_CHAIN_TPL_OVERLOAD seam,
Python bindings only); guide: templates.md "Member function templates".

**Alias-welded instantiations (the sweep route).** `members_of(ns)` never
enumerates a specialization, so a namespace-scope `using IntBox = Box<int>;` is
how one enters `weld_namespace` — the alias is both the C++ spelling and the
target name. Carriage: an alias branch FIRST in bind_namespace (gcc's
`is_class_type(alias)`==true would let the class branch swallow it);
`names_template_specialization` / `alias_welded_for` / `alias_marks_admissible`
live in reflect.hpp. Rules: alias may carry ONLY weld/weld_as, each taking
PRECEDENCE over the template's (alias weld REPLACES the lang set — the
third-party-template opt-in; alias weld_as → template weld_as → styled alias
identifier via detail::alias_bound_name); other marks → static_assert; two
participating aliases of one specialization → static_assert
(detail::sole_alias_of_target — compares by IDENTIFIER: gcc-16 collapses `==` on
alias reflections of the same type); alias to a welded NON-template type →
static_assert (would double-register; weld_as is the rename tool). bind_type
gained `Decl` NTTP (default info{}) — skips the weld participates-assert when
alias-driven, and `make_class_of` prefers a rod's extended
`make_class<T, Decl, Bases>` via requires (a static HELPER, not a lambda:
consteval-only info locals escalate lambdas under P2564). Spelling-aware rods
implementing the extended form: trampolines (renders `: ::ns::IntBox` +
`trampoline_for<::ns::IntBox>`; bare specialization → static_assert pointing at
the alias route) and luacats (records qualified_name(Decl) for the rename table —
qualified_name(^^Box<int>) collapses to the bare namespace and corrupted the
module-root line). Direct `weld_type<Box<int>>(m, "name")` unchanged (type params
dealias — the alias is unrecoverable there). Tests: tests/common/cpp/templates.hpp
↔ test_templates.py + templates_spec.lua (all four runtime rods); trampolines:
Cauldron/IntCauldron (generated) + Gauge/IntGauge (hand-written) in
gen_trampolines.hpp / overridable.hpp; luacats golden (stubdemo.Pair); compile
locks tests/core/weld_alias.cpp + trampoline_slots.cpp (alias render); neg:
tests/python/pybind11/cpp/neg/alias_{forbidden_mark,plain_welded,duplicate}.cpp.

**Opaque, reference-semantic containers (the alias route, specialized).** By
default a `std::vector<T>`/`std::map<K,V>` crosses to Python by COPY (pybind11
`<pybind11/stl.h>` / nanobind `<nanobind/stl/…>`): every read snapshots to a
`list`/`dict`, mutation never reaches C++. Welding a namespace-scope alias to the
container instead binds it OPAQUE (by reference): `using IntVector
[[=welder::weld(lang::py)]] = std::vector<int>;` — the exact template-instantiation
alias mechanism above, but the carriage routes it to the rod's `bind_container`
hook (`py::bind_vector`/`bind_map`, `nb::bind_vector`/`bind_map`) instead of
`bind_type`. Mutation writes through (a `def_readwrite`/`def_rw` member hands out a
live reference, so `obj.v.append(x)` persists — NO change to `add_field`), `append`
= `push_back`, plus slicing/`__getitem__`/`__len__`; a scalar `std::vector` also
exposes `data()` ZERO-COPY (pybind11 `py::buffer_protocol()` → `numpy.asarray`/
`memoryview`/`ctypes.from_buffer`; nanobind has no buffer protocol so `bind_container`
adds an `__array__` returning an `nb::ndarray` view, kept alive via `nb::find(&v)`).
**Scope** = exactly the frameworks' opaque binders: `bind_vector` → `std::vector`
(pybind11's `bind_vector` calls `.reserve()`/`.data()`, so `std::deque` does NOT
qualify — dropped); `bind_map` → `std::map`/`std::unordered_map`. The table +
predicates (`is_reference_container`, `container_kind_of`, `container_is_contiguous`)
+ the `rod_binds_containers<B>` detector live in **`src/welder/containers.hpp`**;
the carriage branch is in `bind_namespace`'s alias dispatch (`carriage.hpp`, right
before the `bind_type` call — `is_reference_container(dealias(mem))` → gate the
element/key/value via `assert_bindable<B, container, L, Resolution>()` then
`B::bind_container`). A REQUIRED opaque declaration (`WELDER_OPAQUE(T)` = each Python
rod's `PYBIND11_MAKE_OPAQUE`/`NB_MAKE_OPAQUE`, `#ifndef`-guarded) at namespace scope
selects the opaque caster (per-TYPE on BOTH frameworks — nanobind's `NB_MAKE_OPAQUE`
coexists with an included stl caster; without it nanobind hard-errors at compile
time naming the fix, pybind11 silently copies). Two-backend asymmetries, both
probe-verified: nanobind numpy = `nb::ndarray` (no buffer protocol); nanobind's
`bind_vector.__getitem__` returns a class element by COPY where pybind11 returns a
reference (a framework detail — the spec tests container-level mutation, which is
consistent, not element-handle mutation). **Python-only** — a container alias welded
for a Lua rod hits a `static_assert` ("Python-rod feature"; `rod_binds_containers`
is false), since the Lua runtimes give containers reference semantics structurally.
Gate: UNCHANGED — the wrapper recursion in `bindable.hpp` still yields bindable-via-
element, so container-typed members keep passing regardless of opaqueness. Tests:
tests/common/cpp/opaque.hpp (Python-only group, like stl.hpp; element/key types kept
DISJOINT from stl.hpp's by-value ones so opaqueness doesn't clobber that group in the
same TU) ↔ test_opaque.py + the opaque cases in test_types.mypy-testing (`Signal.samples`
reveals `FloatVector`, NOT `list[float]` — the type-level tell). numpy is a test dep
(pyproject.toml dev group); pybind11-stubgen's bind_vector/bind_map stubs need a
scoped `no-untyped-def`/`type-arg` mypy relaxation (`*.opaque`), like the enum ones.

**Ordering (the container-first pre-pass).** `bind_namespace` binds
*native-element* container aliases (scalar/string element/key/value) in a PRE-PASS
before anything else (`carriage.hpp`, guarded `rod_binds_containers<B>` +
`container_elements_native<B, type>()` — the latter in containers.hpp, checks each
value arg via `B::has_native_caster`). Reason: an opaque-container **aggregate NSDMI
field**'s default is converted to a Python object at the field-ctor's *def* time, which
needs the container class already registered — so a scalar container a later class uses
must bind first (makes the sweep order-independent, e.g. a generated alias header
included AFTER the classes). Restricted to native-element containers on purpose: a
`std::vector<Welded>` bound ahead of its element would spell the element's raw C++ name
in the container's docstrings/stubs (a pybind11 def-time artifact — stubgen HARD-errors
on it), so welded-class-element containers stay in declaration order (main loop, after
the element). The main loop's container branch binds those non-native ones; native ones
were done in the pre-pass (skipped there).

**The generator (`welder::rods::opaque_containers::rod`).** A build-time text-emitting
rod — the exact model of trampolines/luacats — that removes the hand-written
`WELDER_OPAQUE` + alias boilerplate: it reflects the welded types, finds the scalar
containers they use, and emits a `.hpp` of the declarations + aliases. Rationale: a
namespace-scope `type_caster` specialization is a COMPILE-TIME artifact a runtime rod
can't emit (same wall trampolines hit), and it straddles two scopes (global
`WELDER_OPAQUE` + in-namespace alias) a single macro can't bridge — a generated *file*
places each correctly. Files: `src/welder/rods/python/opaque_containers/`
(`document.hpp` = collector + `derive_name`/`container_spelling`/`opaque_eligible`;
`rod.hpp` = the rod, collecting hooks + `generate<Ns>`; `module.hpp` =
`WELDER_OPAQUE_CONTAINERS_MAIN`; `marks.hpp` = the `by_value` opt-out
(`welder::rods::python::marked_by_value`, mirrors `bound_flat`)). Backend-neutral
(emits the neutral `WELDER_OPAQUE` + `weld(py)` aliases — one header serves both Python
rods); `has_native_caster = true` (permissive gate, like trampolines). Model = BLANKET
over welded types (all scalar containers) + `by_value` opt-out + DERIVED names
(`vector<int>`→`VectorInt`; `::welder::naming::restyle(…, pascal)`). Type spelling =
`display_string_of(dealias(type))` (valid C++, infra args dropped, `std::string` →
`std::__cxx11::basic_string<char>`). **SCALAR-element containers only** (`opaque_eligible`
= all value args fundamental/`basic_string`): they are the zero-copy case AND the
ordering-safe case (always fit the native-first pre-pass); a `vector<Welded>` / nested
container is left by value (documented; hand-write it). CMake:
`welder::opaque_containers` INTERFACE target (`src/welder/rods/CMakeLists.txt`) +
`welder_generate_opaque_containers()` (`cmake/WelderOpaqueContainers.cmake`, mirrors
WelderTrampolines) + top-CMakeLists `include()` — NOT shipped to find_package (parity
with trampolines). Tests: `tests/common/cpp/gen_opaque.hpp` (Python-only; element types
disjoint from stl.hpp/opaque.hpp — the module-wide MAKE_OPAQUE mustn't clobber them;
register hook `#ifdef WELDER_TEST_MODULE_T`-guarded so the generator TU reflects but
doesn't bind) + `tests/python/gen_opaque_gen.cpp` (the generator TU, wired in
tests/python/CMakeLists.txt exactly like gen_trampolines, header #included in both
bindings.cpp) + `test_gen_opaque.py` (both rods) + `tests/core/opaque_containers.cpp`
(compile-lock for derive_name/spelling/eligibility — covers MAPS, which the runtime
group omits: pybind11-stubgen mis-qualifies bind_map view types across submodules
`gen_opaque` vs `opaque`, a name-defined stub break). `*.gen_opaque` shares the opaque
mypy relaxation. Known limitation: a `vector<Welded>` used as an aggregate NSDMI field
can't be auto-generated (topological order the pre-pass can't give) — `by_value` it.

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

## Return-value policy & keep_alive — `return_policy` / `keep_alive`
Two per-callable call policies, resolved **per overload** (read off each `Fn`, not
the group).

- **Vocabulary** (`annotations.hpp`, std-free): `enum class rv_kind` (welder:: scope,
  next to `policy_kind`) with the pybind11/nanobind union — `automatic`,
  `automatic_reference`, `take_ownership`, `copy`, `move`, `reference`,
  `reference_internal`, `none`; user-facing constants in `namespace welder::rv`.
  `detail::return_policy_spec { unsigned mask; rv_kind kind; }` (masked like
  `weld_as`) and `detail::keep_alive_spec { unsigned nurse, patient; }` (not
  language-scoped). Factories: `return_policy([lang…,] kind)` (a `return_policy_mask`
  / `return_policy_kind` pack-walk mirroring `weld_as_mask`/`weld_as_name`) and
  `keep_alive(nurse, patient)` (repeatable).
- **Readers/validation** (`reflect.hpp`): `return_policy_of(fn, L) -> rv_kind`
  (plain `annotations_of_with_type` idiom — the spec is non-templated; first mask
  covering `L` wins, else `automatic`); `validate_return_policy<Fn, L>()` — a
  consteval that hard-errors (throws `diag::dangling_return_policy`) when a
  reference-category kind meets a non-pointer/non-reference `return_type_of(Fn)`.
  `keep_alive_pairs<Fn>()` (`bind_traits.hpp` detail, has `<array>`) materializes the
  `(nurse, patient)` pairs as a splice-ready static array.
- **Rod consumption:** both Python rods map in `_def_function<Fn>` — pybind11
  `_return_value_policy(rv_kind)` (static_asserts against `none`), nanobind
  `_rv_policy(rv_kind)` (has `none`). The policy is **always appended** to the
  `.def(...)` extras (mapped `automatic` == the framework default, so unannotated
  calls are unchanged), and `keep_alive` splices via a second index pack `K...` as
  `py::/nb::keep_alive<ka[K].nurse, ka[K].patient>()...`. Both rods (and both Lua
  rods) call `validate_return_policy<Fn, language>()` at their per-overload bind
  site — sol2 `_register_named`/`_register_operator`, LuaBridge3
  `_add_function`/`_add_static_function` (a `(…, ...)` fold over `Grp[I]`) — so the
  contradiction check is uniform; the Lua rods otherwise **ignore** the policy
  (ownership is structural: value → VM-owned copy/move, pointer/reference →
  non-owning view) and have no `keep_alive` analogue.
- **Tests:** `tests/common/cpp/retpolicy.hpp` (`Owner::view`=reference_internal vs
  `snapshot`=copy; `Registry::track` keep_alive, py-only) ↔ `tests/python/test_retpolicy.py`
  (reference vs copy divergence + keep_alive, gc-based) and `tests/lua/spec/retpolicy_spec.lua`
  (structural reference — policy ignored — for both Lua rods). Negative-compile:
  `tests/python/pybind11/cpp/neg/return_policy_dangling.cpp` (`negcompile.return_policy_dangling`,
  the reference-to-temporary hard error).

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
  rather than the last registered winning. The group is computed by the CARRIAGE
  from its resolution (bind_traits overload-set machinery; the old
  rods/lua/overloads.hpp is gone) and handed to the rod whole — the Python rods
  loop it with chained `.def`s. A same-named member in a derived class still hides
  the base's (C++ name-hiding), unchanged.
- **Constructors, all at once** (both Lua rods want the whole set): the driver's
  single `add_constructors` call becomes sol2's `sol::constructors` assignment /
  LuaBridge3's `addConstructor<Sig…>()`. Both expose the call
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
Further languages are designed-for but not yet implemented; so are STATIC
properties (a marked static accessor is a designed error — see the properties
section). (Enums, custom type converters, the Lua/sol2 rod, sol2 overload
grouping, live sol2 namespace variables, the LuaCATS stub emitter, and
method-backed properties now are.) Remaining sol2 rod enhancement: LuaJIT's 5.1 operator-map branch. LuaCATS stub: overloaded methods/constructors/free functions now render as
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
