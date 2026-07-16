# Annotation vocabulary

Everything welder does is driven by attributes in the `welder::` namespace, spelled
with P3394's `[[=…]]` annotation syntax. There are only a handful.

| Annotation | Meaning |
|---|---|
| `weld(lang…)` | Languages this type is exposed to. **Required to bind.** |
| `policy::automatic` | *(default)* Greedy: reflect every member unless excluded. |
| `policy::opt_in` | Conservative: bind only members marked `include`. |
| `policy::weld_protected` / `…(lang…)` | Admit the type's **protected** members into resolution (combinable with `automatic`/`opt_in`). Private members never bind. |
| `mark::exclude` | Exclude member from **all** welded languages. |
| `mark::exclude(lang…)` | Exclude member from the listed languages only. |
| `mark::include` / `mark::include(lang…)` | Opt a member in (meaningful under `opt_in`). |
| `mark::only(lang…)` | The **complete** set of languages this member may bind for — closed-world counterpart of `exclude`; under `opt_in` it is also the opt-in. Always called with ≥ 1 language. |
| `mark::trust_bindable` / `…(lang…)` | Vouch that a member's type / callable signature is representable outside welder's view. |
| `trust_bindable<T> = true` | Type-level form: trust `T` everywhere it appears. |
| `doc("text")` | Docstring for a class / namespace / function / parameter / data member. |
| `returns("text")` | Documents a function's return value. |
| `tparam("T", "text")` | Documents a template parameter (repeatable, ordered). |
| `weld_as([lang…,] "name")` | Force this entity's target name **verbatim**, bypassing the [name style](naming.md). The name is last; any languages it applies to come first (none = all). |
| `return_policy([lang…,] rv::kind)` | How a callable's returned object is owned/converted — see [Return policies & lifetimes](return-policies.md). |
| `keep_alive(nurse, patient)` | Tie one call participant's lifetime to another's — see [Return policies & lifetimes](return-policies.md#keep_alive). |

!!! example "In the cookbook"

    [Recipe 02 — Discovery rules](../cookbook/discovery.md) exercises most of this
    vocabulary in one runnable module: policies, marks, namespace pruning and
    `weld_as`. [Recipe 07](../cookbook/multilang.md) adds the per-language pieces
    (`mark::only`, per-language `weld_as`, `mark::trust_bindable`).

## `weld` — the discovery marker

`weld` does two things: it declares a type **discoverable** (an independently
registered entity welder may bind, e.g. when walking a namespace), and it lists the
**languages** it is exposed to.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]  // py + lua
Widget { /* … */ };
```

A `lang` is stored as a bit in an `unsigned` mask, and the value space is **open**:
`welder::lang::py` / `welder::lang::lua` name the shipped languages, while
`welder::user_lang<Slot>` mints an identity for a language welder doesn't ship —
usable everywhere a `lang` is (see
[Binding a new language](extending.md#binding-a-new-language)). `weld` is
*required*: a type with no `weld` binds to nothing.

!!! info "`weld` is not an inheritance directive"

    It marks an entity as independently registrable — the most-derived type's
    `weld` drives which languages bind, and a base *need not* be welded. See
    [Inheritance](inheritance.md).

## `policy` — greedy or conservative

The policy on a type decides the default for its members:

=== "`automatic` (default)"

    ```cpp
    struct [[=welder::weld(welder::lang::py)]]           // policy::automatic implied
    Greedy {
        int a;                              // bound
        int b;                              // bound
        [[=welder::mark::exclude]] int c;   // opted *out*
    };
    ```

=== "`opt_in`"

    ```cpp
    struct
    [[=welder::weld(welder::lang::py), =welder::policy::opt_in]]
    Careful {
        [[=welder::mark::include]] int a;   // bound
        int b;                              // NOT bound (nothing opts it in)
    };
    ```

## `policy::weld_protected` — expose the protected surface

By default only **public** members bind. An extensible base — think a framework
widget class full of `protected` NVI hooks and state its subclasses build on —
often *is* its protected surface, so `policy::weld_protected` admits the type's
protected members into resolution. It is a **separate annotation, not a third
policy kind**: it combines freely with `automatic`/`opt_in`, and an admitted
protected member then resolves exactly like a public one (policy, marks,
overload grouping, the bindability gate). Bare it covers all languages; called
with languages it is scoped, like the marks:

```cpp
struct
[[=welder::weld(welder::lang::py), =welder::policy::weld_protected]]
Widget {
    int frame() const { return trim() + width; }   // public, calls the hooks

protected:
    virtual int trim() const { return 10; }        // bound — and, with a
                                                   // trampoline, overridable
    int width{4};                                  // bound, read/write
    [[=welder::mark::exclude]] int scratch{0};     // marks still prune

private:
    int serial{123};                               // NEVER bound (see below)
};
```

No publicist wrapper, no generated shim: welder binds through spliced
pointers-to-member, which C++26 reflection exempts from access checking once
the members were enumerated (the access decision happens at query time, via
`std::meta::access_context`).

It composes with the rest of the machinery:

- **Trampolines.** A protected virtual was *already* an overridable trampoline
  slot; with `weld_protected` it also binds as a callable method, so a Python
  subclass can call, override, and fall back to it — the full NVI story.
- **Templates.** Put it on the class template; like every annotation it is read
  through each instantiation, so an
  [alias-welded instantiation](templates.md) binds its protected members too.
- **Third-party libraries** can't carry the annotation. The tack-welding
  resolution has a knob instead —
  `welder::carriages::greedy_resolution<true>` admits protected members for the
  whole pass — and a [custom resolution](extending.md#custom-traversal-resolutions-and-carriages) can
  arbitrate per member via the optional `protected_participates` hook.

!!! warning "Private is not a policy"

    `weld_protected` never reaches **private** members, and neither can a
    custom resolution: welder hard-wires private out before any hook runs.
    Exposing a class's private implementation is a design violation, not an
    option.

!!! note "Protected constructors stay unbound (for now)"

    There is no pointer-to-member for a constructor, so `weld_protected` does
    not admit protected **constructors**. A type whose *default* constructor is
    protected still constructs from Python when it registers a
    [trampoline](inheritance.md#overriding-virtual-methods-from-python) (the
    trampoline subclass may call it); non-default protected constructors are on
    the roadmap via generated trampoline forwarding constructors.

## `mark` — per-member overrides

`exclude`, `include` and `only` are the per-member overrides. `exclude` and
`include` accept an optional language list; with no argument they apply to
**all** welded languages. `only` names the *complete* set of languages the
member may bind for, so it must always be called with at least one:

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
Mixed {
    std::uint32_t first;                                              // bound everywhere
    [[=welder::mark::exclude]] std::uint32_t second;                  // bound nowhere
    [[=welder::mark::exclude(welder::lang::lua)]] std::string third;  // py, not lua
    [[=welder::mark::include(welder::lang::py)]] std::string last;    // opt-in
    [[=welder::mark::only(welder::lang::py)]] std::uint64_t handle;   // py, and ONLY py
};
```

`exclude` and `only` differ in world-view: `exclude(lua)` is **open** — it names
the languages to hide from, and any language it doesn't name (including a
[user-defined one](extending.md#binding-a-new-language) minted later) still
binds. `only(py)` is **closed** — nothing outside its list ever binds, no
matter what languages join the build afterwards. Under `policy::opt_in`, `only`
also counts as the member's opt-in, so no separate `include` is needed.

!!! note "Marks resolve per overload — constructors included"

    Every **overload** of a name carries its own marks: exclude one and its
    siblings still bind (welder hands each rod the surviving overload set whole,
    so this holds on the Lua rods' one-value-per-name tables too). Individual
    **constructors** resolve the same way — under `policy::opt_in`, only
    marked-`include` constructors bind. Two fail-safes back that up: the
    **default constructor** stays outside opt_in's default-out (an implicit one
    has no declaration to mark — though explicit marks on a *declared*
    `T() = default;` are honored, so you *can* suppress it); and policy filtering
    that would leave a type with **no constructor at all** is a hard compile
    error rather than a silently uninstantiable class — unless the emptiness is
    explicit (`mark::exclude` on every constructor declares a factory-only
    surface, and compiles).

    The **copy constructor** follows the default constructor's pattern (it never
    binds as an init overload — the Python rods spell it `__copy__`/`__deepcopy__`):
    an implicit one rides along whenever the type is copy-constructible, and a
    declared one's explicit marks are honored, `exclude` included. The **move
    constructor** never binds — an `include`/`only` mark on one is a hard
    compile error (`exclude` stays a harmless no-op). See
    [Copy and move constructors](binding-types.md#copy-and-move-constructors).

## The resolution rule

For a given language `L`, `member_bound(member, L, policy)` decides:

```mermaid
flowchart TD
    S([member, language L]) --> X{excluded for L?}
    X -- yes --> N[false]
    X -- no --> O{an only mark?}
    O -- yes --> M{does it name L?}
    M -- yes --> T[true]
    M -- no --> N
    O -- no --> P{policy?}
    P -- automatic --> T
    P -- opt_in --> I{included for L?}
    I -- yes --> T
    I -- no --> N
    style T stroke:#2e7d32,stroke-width:3px
    style N stroke:#999,stroke-width:2px
```

- Excluded for `L` → **false** (exclude is the strongest word — it beats an
  `only` naming `L` too).
- Else an `only` mark → **true iff** it names `L`, under either policy
  (repeated `only` marks union their languages).
- Else `automatic` → **true**.
- Else (`opt_in`) → **true iff** explicitly included for `L`.

A mask of `0` on an `exclude`/`include` spec is the sentinel for "all languages"
(a bare `mark::only` has no such meaning — "only, for every language" restricts
nothing — and is diagnosed at compile time).

!!! note "Naming deviation"

    The original sketch used `policy::auto`, but `auto` is a reserved keyword, so
    welder spells it `policy::automatic`. Under `automatic`, an `include` mark is
    redundant (a diagnostic for that is a TODO).

## `doc`, `returns` and `tparam` — documentation

Documentation is part of the vocabulary too. `doc("text")` is the summary
docstring — on a class, namespace, function, function parameter, or data member.
`returns("text")` documents a function's return value (a return value is not a
reflectable entity, so its doc rides on the function as a distinct annotation).
`tparam("T", "text")` documents a template parameter:

```cpp
[[
  =welder::weld(welder::lang::py),
  =welder::doc("Scale a length by a factor."),
  =welder::returns("the scaled length")
]]
double scale(
    [[=welder::doc("the length to scale")]] double length,
    [[=welder::doc("the multiplier")]] double factor);
```

Where the text lands — the Python `__doc__` under a pluggable style, the
generated [stubs](stubs.md), the C++ reference — is the subject of
[Docstrings](docstrings.md).

## `return_policy` and `keep_alive` — ownership & lifetimes

When a bound call returns an object, `return_policy` picks who owns it —
welder's backend-neutral spelling of pybind11's `return_value_policy` /
nanobind's `rv_policy` — and `keep_alive(nurse, patient)` ties one call
participant's lifetime to another's:

```cpp
struct [[=welder::weld(welder::lang::py)]] Owner {
    [[=welder::return_policy(welder::rv::reference_internal)]]  // a live, non-owning view
    Inner& view() { return inner_; }

    [[=welder::keep_alive(1, 2)]]   // keep the item (arg 2) alive with `this` (arg 1)
    void track(Item& i);
};
```

welder forwards the chosen kind to the backend verbatim — the frameworks'
documented semantics are unchanged — and adds compile-time sanity validation on
top (a reference-category policy on a by-value return is a hard error in every
language). The full story — the `welder::rv::` kinds, per-language scoping, how
the Lua rods decide ownership structurally instead, and `keep_alive`'s index
convention — lives on [Return policies & lifetimes](return-policies.md).

The two `trust_bindable` escape hatches are covered in
[Trust & type casters](trust-casters.md), and `weld_as` — the verbatim per-entity
rename — in [Naming conventions](naming.md), alongside the pluggable name styles
that reshape identifiers into a target language's convention.
