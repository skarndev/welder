# The bindability gate, trust hatches & type casters

Read when: touching `bindable.hpp`, the `static_assert` bindability gate, the
`trust_bindable` escape hatches, or pybind11 `type_caster` interplay. User-facing
version: `docs/content/guide/bindability.md` and `trust-casters.md`.

## The gate ("pybind11-convertible")
Every surface welder is about to bind (data member, parameter, return type,
namespace variable) must be a type pybind11 can convert to a *meaningful* Python
value; otherwise it is a **hard compile error** (`static_assert` in
`assert_bindable`, naming the offending type) — never a silent skip, since binding
such a type yields a dead attribute *and* a stub referencing an unimportable type
(breaking pybind11-stubgen). The fix in the message: weld the type, give it a
pybind11 `type_caster`, or `mark::exclude` the member.

The engine is **backend-agnostic** and lives in the core (`bindable.hpp`): a
reflection-driven `stl_wrappers` table (`{^^std::vector, 1}`, `{^^std::map, 2}`,
`0` = all args for `tuple`/`variant`, …) matched via `template_of`, which recurses
the value arguments of the STL containers, `optional`, `pair`/`tuple`/`variant`
and the smart-pointer holders (so `std::vector<Unwelded>` is caught, not just a
bare `Unwelded`). Reflection can enumerate a specialization's arguments but not
tell which are value-bearing vs. infrastructure (allocator/comparator/hasher/
deleter/extent), so the per-wrapper leading-arg *count* is the one thing the table
still records. `bindable<B, T, L, Reg>()` folds these: a wrapper binds iff its value
args do; a native/user-caster type binds as-is; otherwise it's a
registration-needing class/enum, bound iff the **registration oracle** `Reg`
promises a registration. `Reg` defaults to `welder::welded_registration`
(`counts_as_registered` = `welded_for` — the historical behavior, used by every
direct `bindable`/`assert_*` call); the carriage passes its *Resolution* instead
(the `resolution` concept now requires the `counts_as_registered(type, L)` hook):
`marker_resolution` → `welded_for` (identical), `greedy_resolution` → any
*complete*, non-excluded class/enum — a tack-welded library's own types pass in
its signatures with no trust hatch (the same greedy pass registers them).
**Class-NESTED types:** both shipped oracles extend the answer via
`detail::nested_type_registered<Resolution>` (carriage.hpp) — a class-scoped
type counts iff it resolves as a *member* of a counting outer (the outer's
policy + the type's own marks, `member_access_admitted`, has_identifier +
complete, recursing into the enclosing chain) — the EXACT mirror of the
carriage's `bind_nested_types` sweep, so the gate promises precisely what the
sweep registers. `welded_for` still short-circuits first under stitch: a nested
type carrying its own `weld` counts even when marked out of the sweep (the
`mark::exclude` + `weld` combo = manual flat registration). A bespoke resolution
that prunes types must mirror its pruning here, nested ones included. Locked by
`compile.nested_types` + `negcompile.nested_excluded_in_signature`.
**MEMBER-ALIAS registrations** ride a second layer: the carriage gates a class's
own members through `detail::scoped_registration<Resolution, Scope>` (Scope =
the bound-into type), which additionally counts types a participating member
alias of Scope registers — the plain oracle can't (an alias is unrecoverable
from the type it names), so cross-class/namespace-level use of an
alias-registered type stays trust territory. Its alias leaf deliberately skips
a gate re-check: `counts_as_registered` is reached only after every other
`bindable()` branch failed, which is exactly the sweep's register-vs-skip
arbiter for aliases. Also note both Python rods force `is_enum_v` into
needs-registration (their frameworks' enum casters aren't the base caster, so
enums would otherwise read "native" and the welded-enum requirement wouldn't
actually gate). The
oracle is deliberately a **pure predicate of the declaration, never a
visited-set**: multi-pass welds (several `weld_*` calls, several tacked
namespaces) and forward references stay order-independent. Consequences, both
tested: a *forward-declared* (incomplete) type still hard-errors
(`negcompile.tack_incomplete_param`) since the walk can't register it; and a
registrable type you never actually tack-weld binds but raises the framework's
unregistered-type error at call time (trust semantics, documented). pybind11
caveat (framework, not welder): docstrings render at def time, so a signature
referencing a type *registered later* spells the raw C++ name in
docstrings/stubs — the shared `foreign` case avoids fwd-decl hoisting for this
reason (comment in tests/common/cpp/namespace.hpp).

## Unions: categorically rejected
Unions NEVER bind (all four backends *can* register one — pybind11 since 2.6.0,
nanobind documented, sol2 ≥3.5.0, LuaBridge3 silently — but none tracks the
active member; reading an inactive member is UB, so welder refuses to
manufacture that surface). Designed hard errors, all locked by tests:
- the gate: `bindable()` returns false for a union AFTER the trust/native-caster
  branches (so `trust_bindable<U>`/a self-contained caster still clear it — the
  hand-registration escape) and BEFORE `counts_as_registered` (so a `weld` mark
  can never vouch — this closed a real hole: a welded-union member used to
  compile clean and die at runtime, since no sweep registers unions).
  `assert_bindable` picks a union-specific message via `detail::mentions_union`
  (mirrors the strip+wrapper recursion; remedy = std::variant / accessors /
  exclude / trust) — locked by `negcompile.union_member`;
- `bind_type` static_asserts `!is_union_type(^^T)` (weld_type on a union used to
  die incidentally in the aggregate-init path) — `negcompile.union_weld_type`;
- the namespace walk diagnoses a `weld`-MARKED union loudly (an unmarked one is
  skipped — tack welding can't edit third-party headers; its uses still gate) —
  `negcompile.union_welded_in_namespace`;
- ANONYMOUS union members (and unnamed bit-fields): `bind_members` skips
  unnamed data members structurally (nothing to name by, no declarator to
  mark — gcc *ignores* a pre-`union` attribute, and marks on the unnamed TYPE
  aren't member marks, so exclusion was unspellable), and
  `aggregate_initializable` counts an unnamed field as non-participating (no
  synthesized field ctor). Named members around the union still bind.
- nested unions: already skipped by the nested-type sweep (`is_class_type`).
The blessed path is std::variant (in the stl_wrappers table; converts natively
on all four rods — nanobind needs `<nanobind/stl/variant.h>`, LuaBridge3
`<LuaBridge/Variant.h>` in the consumer TU). Cross-rod caveat (probe-verified):
into-variant matching is single-pass DECLARATION order on both Lua rods (first
accepting check wins — a Lua number lands in a `double` declared before `int`)
but two-pass (exact, then converting) on the Python rods (exact beats
declaration order) — `variant<double,int>` from `42`: Lua → double, Python →
int. Keep alternatives target-unambiguous. (sol2 3.5.0 DOES contain a
reverse-order variant check_getter, stack_check_get_unqualified.hpp — but it
is unreachable through binding paths: function args, properties and
check_get/optional all route through the forward getter; don't re-document
"sol2 is reverse" from that source reading.) Positive cases:
`tests/common/cpp/unions.hpp` (+ test_unions.py / unions_spec.lua). Guide:
`docs/content/guide/bindability.md` "Unions never bind".

## The one rod-specific leaf
Native-vs-needs-registration is the rod's `has_native_caster<T>`
(`caster_oracle`); pybind11 implements it as `!needs_registration<T>`, i.e. is T's
caster the generic `type_caster_base` fallback (needs a `class_`/`enum_`) vs.
native/self-contained? It is *conservative*: a compile-time read of T's caster
type, so it reports whether T *needs* registration, never whether one will exist —
a hand-registered (`py::class_`) but non-welded type still reads
needs-registration and is rejected (trust hatches are the fix). Only a
*self-contained* user `type_caster` (not derived from `type_caster_base`, e.g. via
`PYBIND11_TYPE_CASTER`) flips native; a `type_caster_base`-derived caster still
needs its class registered. "Native" is also relative to the TU's includes —
`std::complex`/`function`/`chrono`/`filesystem::path` are native only with their
converter header (`<pybind11/complex.h>`, …). *Not* exhaustive for a non-STL
wrapper with its own caster — its elements aren't recursed (an opaque bindable
leaf).

**nanobind** implements the same leaf against its own casters:
`needs_registration<T>` is `nb::detail::is_base_caster_v<nb::detail::make_caster<T>>`
(is_base_caster = "this caster derives from nanobind's `type_caster_base`"). Same
conservative semantics; a self-contained caster is one written with `NB_TYPE_CASTER`
(not deriving from `type_caster_base`), and "native" is relative to which
`<nanobind/stl/*>` converter headers the TU includes. Everything above the leaf —
the STL-wrapper recursion, the trust hatches, the assert messages — is shared.

**sol2** reads sol2's compile-time classification: `needs_registration<T>` is
`std::is_enum_v<T> || sol::lua_type_of<T>::value == sol::type::userdata`. sol2 maps
every type to a `sol::type` (number / boolean / string / table / poly / userdata);
`userdata` is the "needs a usertype registered" bucket, and scalars / `bool` /
strings (`std::string` is native without extra includes, unlike pybind's `stl.h`) /
the sol wrapper types are native. Enums are folded into needs-registration even
though sol2 would convert them as numbers, so a welded enum's name→value table is
required and enum-typed members are gated on it — matching the Python rods. The
STL-wrapper recursion, trust hatches and assert messages are shared as usual.

Negative-compile cases live in `tests/python/pybind11/cpp/neg/`, `tests/python/nanobind/cpp/neg/`
and `tests/lua/cpp/neg/` (`negcompile.*` / `negcompile.nanobind.*` /
`negcompile.sol2_unwelded` CTests, `WILL_FAIL`).

## Escape hatches (trust)
Two hatches cover a type welder can't see is registered (hand-written pybind11
`class_`, a third-party library's bindings) — both backend-agnostic, in the core
vocabulary, and suppressing the gate so the user then owns the registration:

- a member mark `[[=welder::mark::trust_bindable]]` (trusts that member's type / a
  callable's whole signature; `reflect.hpp` `trusted_for`, honored via
  `bindable.hpp` `assert_member_bindable` / `assert_callable_bindable`), and
- a type-level `welder::trust_bindable<T> = true` (trusts T everywhere, folded into
  `bindable()` so it also clears `T*` / `const T&` / `std::vector<T>`).

Tested in `tests/python/pybind11/cpp/trust.hpp` + `test_trust.py`. A richer future point
could map T to a stub type name (`bindable_as<T>`); still TODO.

## Self-contained type casters
**A self-contained pybind11 `type_caster<T>`** (not deriving from
`type_caster_base`, e.g. via `PYBIND11_TYPE_CASTER`) needs *neither* weld nor
trust: it displaces the fallback, so `needs_registration` is false and the gate
passes automatically, and the caster's `const_name` stubs the member/parameter
cleanly (e.g. as `float`). The one requirement (standard pybind11, not
welder-specific): the caster must be visible **before** welder binds any type using
T — gcc-16 defers the point of instantiation to end-of-TU so a later caster in the
*same* TU also works, but that is ill-formed-NDR to rely on; keep the caster ahead
of the bind. Tested in `tests/python/pybind11/cpp/caster.hpp` + `test_caster.py`.
