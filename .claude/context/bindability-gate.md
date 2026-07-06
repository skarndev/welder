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
still records. `bindable<B, T, L>()` folds these: a wrapper binds iff its value
args do; a native/user-caster type binds as-is; otherwise it's a
registration-needing class/enum, bound iff `welded_for`.

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
