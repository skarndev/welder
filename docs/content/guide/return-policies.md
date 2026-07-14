# Return policies & lifetimes

When a bound call returns an object, someone must decide **who owns it** — does
the target language take ownership, copy it, or hold a view into C++ memory, and
what keeps that memory alive? The two annotations on this page are welder's
backend-neutral spelling of that decision: `return_policy` picks the ownership
policy for a callable's return value, and `keep_alive` ties one call
participant's lifetime to another's.

!!! info "welder does not reinterpret the frameworks"

    Both annotations map **one-to-one** onto the Python frameworks' own
    mechanisms — pybind11's
    [`return_value_policy`](https://pybind11.readthedocs.io/en/stable/advanced/functions.html#return-value-policies)
    and `keep_alive<Nurse, Patient>`, nanobind's
    [`rv_policy`](https://nanobind.readthedocs.io/en/latest/api_core.html#rv-policy)
    and `nb::keep_alive` (see nanobind's
    [object ownership guide](https://nanobind.readthedocs.io/en/latest/ownership.html)).
    welder does **not** change their semantics; the kind you choose is forwarded
    verbatim, and the frameworks' documentation stays authoritative. What welder
    adds is [extra sanity validation](#what-welder-validates) at compile time.

## `return_policy`

By default a rod lets its backend pick how a call's return value crosses into the
target language (the backends' `automatic`). `return_policy` overrides that,
per callable:

```cpp
struct [[=welder::weld(welder::lang::py)]] Owner {
    // A live, non-owning view; writing through it writes the C++ object, and the
    // owner is kept alive while the view lives.
    [[=welder::return_policy(welder::rv::reference_internal)]]
    Inner& view() { return inner_; }

    // An independent copy — the caller's edits don't touch the owner.
    [[=welder::return_policy(welder::rv::copy)]]
    Inner& snapshot() { return inner_; }
};
```

### The kinds

The kinds live in `welder::rv::` and mirror the frameworks' sets name-for-name
(pybind11 `return_value_policy::copy` ⇔ nanobind `rv_policy::copy` ⇔
`welder::rv::copy`):

| `welder::rv::` | In one line |
|---|---|
| `automatic` | *(the rod default)* — take ownership of pointers, copy/move values. |
| `automatic_reference` | as `automatic`, but never take ownership of a pointer (the frameworks' internal default for arguments). |
| `take_ownership` | the target language owns the returned pointer and will free it. |
| `copy` | an independent copy, owned by the target language. |
| `move` | as `copy`, but moved from. |
| `reference` | a non-owning view; **you** guarantee the referent outlives it. |
| `reference_internal` | a non-owning view that keeps `this` (the parent object) alive — pairs an implicit [`keep_alive`](#keep_alive) with `reference`. |
| `none` | refuse conversions that would create a new object (**nanobind-only** — diagnosed at compile time on the pybind11 rod). |

For what each kind means in detail — caveats included — read the framework's own
page linked above; welder deliberately adds nothing to those semantics.

### Scoping a policy per language

Like [`weld_as`](naming.md#weld_as-force-a-name-verbatim), the kind is the last
argument, and zero or more leading `lang` markers scope it (none = all
languages). Repeat the annotation to give different languages different
policies:

```cpp
[[=welder::return_policy(welder::lang::py, welder::rv::take_ownership)]]
Thing* make();
```

## What welder validates

The one thing welder adds on top of the frameworks is refusing, at **compile
time**, combinations that are wrong in every backend or in the one you picked:

- A **reference-category kind on a by-value return** (`reference` /
  `reference_internal` on a function returning `T`) would create a view of a
  temporary — a dangling reference. Hard error in **every** language, including
  the ones that ignore the annotation.
- **`rv::none` on the pybind11 rod** — the kind exists only in nanobind, so
  welding it through pybind11 is diagnosed rather than silently degraded.

## How the Lua rods decide ownership

The garbage-collected Lua runtimes (sol2, LuaBridge3) expose no return-value
policy knob: ownership follows the C++ **return type**, structurally. The Lua
rods therefore **ignore** `return_policy` (exactly as they ignore
[`doc`](docstrings.md)) — but the validation above still runs for them:

| C++ return type | In Lua |
|---|---|
| value (`T`) | a VM-owned copy/move — the Lua GC frees it |
| pointer / reference (`T*`, `T&`) | a non-owning view — C++ keeps ownership |

## `keep_alive`

`keep_alive(nurse, patient)` keeps one call participant alive at least as long
as another, using pybind11/nanobind's index convention — `0` is the return
value, `1` the first argument (a method's implicit `this`), `2` the second, and
so on: the *patient* is kept alive until the *nurse* is collected.

```cpp
struct [[=welder::weld(welder::lang::py)]] Registry {
    // Keep the appended item (arg 2) alive as long as the registry (arg 1 = this).
    [[=welder::keep_alive(1, 2)]]
    void track(Item& i);
};
```

It is repeatable — declare one annotation per lifetime dependency — and maps to
pybind11/nanobind `keep_alive<Nurse, Patient>`. Like `return_policy`, it is a
Python-binding concept: the Lua rods have no equivalent and ignore it.

Next: [Docstrings](docstrings.md).