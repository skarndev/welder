# The resolution algorithm

This page is the single map of **how welder decides what ends up in a binding**
— every entity kind, every rule, in the order the carriage applies them. The
[guide](guide/index.md) explains each feature narratively; this is the
reference picture for when you need to know *exactly* why something did or
didn't bind.

Three separable questions are asked about everything welder touches, each owned
by a different layer:

| # | Question | Layer | Where |
|---|---|---|---|
| 1 | Does this entity **participate**? | the *resolution* (stitch / tack / bespoke) | `marker_resolution` / `greedy_resolution` |
| 2 | May this member's **access level** be exposed? | access admission | `member_access_admitted` |
| 3 | Can its types **cross the language boundary**? | the [bindability gate](guide/bindability.md) + its registration oracle | `bindable()` / `counts_as_registered` |

A "no" from 1 or 2 means the entity is silently left out (that is what marks
are for). A "no" from 3 is a **hard compile error** — welder never emits a
binding that would be dead at runtime.

Everything below describes the default **stitch** (marker-directed) resolution;
the [tack-welding differences](#tack-welding-the-greedy-differences) are
collected at the end.

## Entry points

```mermaid
flowchart LR
    WT(["weld_type&lt;T&gt;"]) --> P1{"welded for L?"}
    WF(["weld_function&lt;Fn&gt;"]) --> P1
    WV(["weld_variable&lt;Var&gt;"]) --> P1
    P1 -- yes --> BIND[bind it]
    P1 -- no --> ERR[hard error]
    WN(["weld_namespace&lt;Ns&gt;<br/>weld_module&lt;Ns&gt;"]) --> SWEEP[the namespace sweep]
    style ERR stroke:#c62828,stroke-width:3px
```

The manual entry points check only the `weld` marker — calling them *is* the
statement of intent, so the entity's own resolution marks are not re-consulted
(a `weld_function<Fn>` still gathers Fn's *participating* same-name overloads).
The namespace entry points hand everything to the sweep.

## The namespace sweep

`weld_namespace` visits the namespace's members **in declaration order** and
dispatches on kind. `pol` is the namespace's `policy` (default `automatic`);
"resolves" means [member resolution](#member-resolution-and-access-admission)
below.

```mermaid
flowchart TD
    M([namespace member]) --> K{kind?}

    K -- "class / enum type" --> C{"welded for L,<br/>and resolves under pol?"}
    C -- yes --> CB["bind_type / bind_enum<br/>(the class interior below)"]
    C -- no --> SKIP1[skip]

    K -- "alias to a template<br/>specialization" --> A{"weld on the ALIAS<br/>(precedence, replaces) —<br/>else the template's, read<br/>through the instantiation?<br/>+ resolves under pol"}
    A -- yes --> AB["bind_type, named by the alias<br/>(sole alias per target, enforced)"]
    A -- no --> SKIP2[skip]

    K -- "alias to a plain type" --> PA{"target welded?"}
    PA -- yes --> ERR1["hard error<br/>(would register twice)"]
    PA -- no --> SKIP3[skip]

    K -- "free function" --> F{"welded for L,<br/>and resolves under pol?"}
    F -- yes --> FB["bind as one overload GROUP<br/>(each overload resolves individually)"]
    F -- no --> SKIP4[skip]

    K -- "variable" --> V{"welded for L,<br/>and resolves under pol?"}
    V -- yes --> VB["const → value snapshot<br/>mutable → live property"]
    V -- no --> SKIP5[skip]

    K -- "nested namespace" --> N{"resolves under the<br/>PARENT's pol, and holds<br/>bound content?"}
    N -- yes --> NB["submodule; recurse<br/>under its OWN policy"]
    N -- no --> SKIP6[skip]

    style ERR1 stroke:#c62828,stroke-width:3px
```

Two asymmetries worth memorizing:

- **Leaf entities need a `weld`; namespaces never do.** A namespace is a scope,
  not a bindable thing — it becomes a submodule when something inside it binds.
- **Only `weld` / `weld_as` may sit on a namespace-scope alias** — every other
  mark belongs on the class template, where it applies to all instantiations
  (diagnosed, not ignored).

## The class interior

Once a type is being bound (from the sweep, from `weld_type`, or as a nested
type), its interior is walked in a fixed order — **nested types first**, so a
member whose default argument or signature names one finds it registered:

```mermaid
flowchart TD
    T([a bound type T]) --> B["native bases → base clause<br/>(welded ancestors, deduplicated);<br/>NON-welded bases queue for flattening"]
    B --> NT["1 — nested member types + member aliases<br/>(the diagram below; recursive)"]
    NT --> CT["2 — constructors, as ONE set:<br/>default (if constructible + admitted) ·<br/>each participating public non-copy/move ctor ·<br/>synthesized field ctor for a baseless aggregate<br/>whose fields ALL participate"]
    CT --> FS{"filtering left NO constructor,<br/>but 'automatic' would have some?"}
    FS -- yes --> ERR2["hard error<br/>(silently uninstantiable)"]
    FS -- no --> MEM["3 — data members, methods, operators:<br/>flattened bases recursed FIRST (derived wins<br/>name clashes), each member through access<br/>admission + member resolution, callables<br/>emitted as whole overload groups"]
    style ERR2 stroke:#c62828,stroke-width:3px
```

Constructor fine print: an *implicit* default constructor has no declaration to
mark, so it is exempt from `opt_in`'s default-out; explicit marks on a declared
one are honored. `mark::exclude`-ing every constructor is the deliberate
factory-only escape (the fail-safe doesn't fire, because `automatic` would also
find none admitted).

## Member resolution and access admission

Every *member* decision — field, method, operator, constructor, enumerator,
nested type, member alias — composes the same two small machines. Access
admission runs first:

```mermaid
flowchart LR
    AM([member]) --> PUB{public?}
    PUB -- yes --> IN[admitted]
    PUB -- no --> PROT{protected?}
    PROT -- no --> OUT["never<br/>(private is hard-wired out —<br/>no resolution can readmit it)"]
    PROT -- yes --> HOOK{"resolution declares<br/>protected_participates?"}
    HOOK -- yes --> HOOKD{hook says yes?}
    HOOK -- no --> ANN{"declaring class carries<br/>policy::weld_protected for L?"}
    HOOKD -- yes --> IN
    HOOKD -- no --> OUT
    ANN -- yes --> IN
    ANN -- no --> OUT
    style OUT stroke:#c62828,stroke-width:2px
```

then the mark/policy resolution (`member_bound`), **per overload** — a mark on
one constructor or one overload prunes exactly that one:

```mermaid
flowchart LR
    R([admitted member]) --> EX{"exclude<br/>covering L?"}
    EX -- yes --> NO[out]
    EX -- no --> ONLY{"an only(...)<br/>mark present?"}
    ONLY -- yes --> OL{"names L?<br/>(repeats union)"}
    OL -- yes --> YES[in]
    OL -- no --> NO
    ONLY -- no --> POL{"scope policy?"}
    POL -- automatic --> YES
    POL -- opt_in --> INC{"include<br/>covering L?"}
    INC -- yes --> YES
    INC -- no --> NO
```

`only` is the closed-world mark (the *complete* language set, and the opt-in
under `opt_in`); `exclude` beats it; a bare `only()` is diagnosed.

## Nested types and member type aliases

Both are class members, so access admission + member resolution above apply —
the *outer's* policy, the member's own marks, **never a `weld` of their own**.
What differs is the final arbiter:

```mermaid
flowchart TD
    MT([member of T]) --> KIND{kind?}

    KIND -- "declared nested<br/>class / enum" --> DN{"named, complete,<br/>not a union?"}
    DN -- no --> S1[skip]
    DN -- yes --> DNR{"admitted +<br/>resolves under<br/>T's policy?"}
    DNR -- yes --> REG1["register under T<br/>(module.T.Inner);<br/>recurse into ITS interior"]
    DNR -- no --> S2[skip]

    KIND -- "member type alias" --> MA{"named class/enum<br/>target, complete?"}
    MA -- no --> S3[skip]
    MA -- yes --> MAM{"only weld_as / exclude /<br/>include / only on the alias?"}
    MAM -- no --> ERR3[hard error]
    MAM -- yes --> MAR{"admitted +<br/>resolves under<br/>T's policy?"}
    MAR -- no --> S4[skip]
    MAR -- yes --> GATE{"target FAILS the<br/>bindability gate?"}
    GATE -- "no (castable, welded,<br/>or already registered)" --> S5["skip — registering again<br/>would be redundant<br/>or a duplicate"]
    GATE -- yes --> REG2["register the target under T,<br/>named by the ALIAS<br/>(one alias per target, enforced);<br/>recurse into ITS interior"]

    style ERR3 stroke:#c62828,stroke-width:3px
```

Consequences that fall straight out of the gate-as-arbiter rule:

- ordinary `using value_type = std::vector<T>;` conventions cost nothing (the
  wrapper is castable → skipped);
- an alias to a welded or sibling-nested type never double-registers;
- `mark::exclude` on a declared nested type + an alias to it = the class-scope
  **rename escape** (the exclude makes the target fail the gate, the alias
  re-registers it under its own name);
- under tack welding *every* complete type passes the greedy gate, so member
  aliases never fire there.

A nested type registers exactly once, with its **declaring** class — a
flattened (non-welded) base's nested types are *not* re-registered on each
derived type; a flattened signature naming one fails the gate until the base is
welded.

## The bindability gate and its registration oracle

Every surface of everything that binds — each member's type, each parameter,
each return — runs the [bindability gate](guide/bindability.md): STL wrappers
recurse into their value arguments, `trust_bindable` and native casters pass,
a **union hard-errors with its own diagnostic** (no sweep can ever register
one — reading an inactive member is UB; use `std::variant`, see
[Unions never bind](guide/bindability.md#unions-never-bind)), and what remains
is a registration-needing class/enum, answered by the **registration oracle**:

```mermaid
flowchart TD
    Q(["oracle: is this class/enum<br/>registered for L?"]) --> W{"welded for L?"}
    W -- yes --> OK[registered]
    W -- no --> NEST{"class-scoped<br/>(a nested type)?"}
    NEST -- yes --> NC{"named + complete +<br/>admitted + resolves under<br/>the OUTER's policy?"}
    NC -- yes --> RECUR{"…and the OUTER<br/>itself registered?<br/>(recurse)"}
    RECUR -- yes --> OK
    RECUR -- no --> SCOPE
    NC -- no --> SCOPE
    NEST -- no --> SCOPE{"gating a class's OWN member?<br/>(the scope-aware wrapper)"}
    SCOPE -- yes --> ALIAS{"a participating member<br/>alias of THAT class<br/>names this type?"}
    ALIAS -- yes --> OK
    ALIAS -- no --> NO["not registered →<br/>hard error at the use site<br/>(weld it, trust it, or exclude<br/>the member)"]
    SCOPE -- no --> NO
    style OK stroke:#2e7d32,stroke-width:3px
    style NO stroke:#c62828,stroke-width:3px
```

The oracle is a **pure predicate of declarations** — never a visited-set — so
multi-pass welds and forward references stay order-independent. Two structural
blind spots follow from "an alias is unrecoverable from the type it names",
and both resolve to [`trust_bindable`](guide/trust-casters.md):

- a type welded only through a **namespace-scope alias** (the third-party
  template opt-in) is invisible to the oracle in signatures;
- a **member-alias** registration is visible only to the registering class's
  own members (the scope-aware wrapper) — not across classes, not at namespace
  level.

## The kinds at a glance

| Entity | Discovery | Resolves under | Named by | Duplicates diagnosed |
|---|---|---|---|---|
| namespace-scope class/enum | its `weld` | the namespace's policy + own marks | identifier → style → `weld_as` | — |
| alias-welded specialization | alias `weld` (precedence) or the template's | the namespace's policy + the instantiation's marks | the alias (its `weld_as` → the template's → styled identifier) | two aliases of one specialization |
| free function / variable | its `weld` | the namespace's policy + own marks | identifier → style → `weld_as` | — |
| nested namespace | never welded | the *parent's* policy; recursed under its own | identifier → style → `weld_as` | — |
| field / method / operator / enumerator | rides the outer's `weld` | the outer's policy + own marks, per overload | identifier → style → `weld_as` | — |
| constructor | rides the outer's `weld` | symmetric, + default-ctor exemption + no-ctor fail-safe | — | — |
| **nested class/enum** | rides the outer's `weld` | the outer's policy + own marks + access admission | identifier → style → `weld_as` | — |
| **member type alias** | rides the outer's `weld` | the outer's policy + alias marks, **iff the target fails the gate** | the alias (its `weld_as` → the target's → styled identifier) | two aliases of one target, per class |

Everywhere a name is produced, a **call-site override** (`weld_type<T>(m,
"Name")`) beats even `weld_as`.

## Tack welding: the greedy differences

[`greedy_resolution`](guide/extending.md#custom-traversal-resolutions-and-carriages)
keeps the whole shape above and changes exactly these knobs:

- **`weld` markers are ignored** — every reflectable type / function / variable
  participates, and namespaces recurse when they hold anything *bindable*
  (marks that happen to be present still prune, via the same member resolution).
- **Every public base is flattened** (`is_native_base` = false) — no reliance
  on a base being separately registered.
- **The registration oracle accepts any complete, non-excluded class/enum** —
  the tacked library's own types pass in its signatures without trust hatches;
  the nested-type chain applies unchanged. The trust is real: a registrable
  type you never actually tack surfaces as the framework's unregistered-type
  error at call time, and a forward-declared type still hard-errors.
- **Member aliases never participate** — everything complete passes the greedy
  gate, so there is nothing left for an alias to register.
- **Protected members** get a whole-pass knob (`greedy_resolution<true>`) since
  a third-party header cannot carry `policy::weld_protected`.

A bespoke resolution may retune any of this — with one obligation: whatever it
*prunes* it must also deny in `counts_as_registered` (nested types included),
or the gate will vouch for registrations the sweep never makes. See
[Extending welder](guide/extending.md).