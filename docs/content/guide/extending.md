# Extending welder

welder's core is deliberately open at three seams, all joined by static
polymorphism and concept-checked at compile time:

- a **rod** — a new binding backend (a new framework, or a whole new target
  language);
- a **resolution** — a new *which-participates* policy, injected into the
  traversal carriage;
- the **entry point itself** — `welder::welder` can be subclassed to compose
  bespoke routines from the same gated building blocks.

None of them require touching welder's sources, and none require your code to
live in a welder namespace. This page walks through each.

## Writing a rod

A rod is a **stateless struct of static members** that satisfies the
`welder::rod` concept. It supplies only the *emission
primitives* — how to register a class, a method, a module attribute in your
framework — and never re-implements traversal, annotation semantics, name
resolution or the bindability gate: those stay in the core, which drives your
hooks. If your struct satisfies the concept, every `welder::welder<YourRod>`
entry point works with it unchanged.

The concept *shape*-checks the whole contract, so a rod that omits or mis-signs
a hook fails with a clear "not a `welder::rod`" instead of a deep error inside
the driver. The development loop is correspondingly simple: keep two
`static_assert`s next to your rod and make them pass —

```cpp
static_assert(welder::caster_oracle<my_rod>);
static_assert(welder::rod<my_rod>);
```

### The contract at a glance

**Associated statics and types** — who you are and what your handles look like:

```cpp
struct my_rod {
    static constexpr welder::lang language{welder::lang::py}; // the target language
    using module_type = my_module_handle;                     // passed by reference

    template <class T> using class_handle_type = my_class_handle; // what make_class yields
    template <class E> using enum_handle_type  = my_enum_handle;  // what make_enum yields

    // The caster oracle: can the framework natively convert T *without* welder
    // registering it? true → scalars/strings/STL your framework ships casters
    // for; false → the bindability gate requires T to be welded.
    template <class T>
    static constexpr bool has_native_caster = /* ... */;
```

**Class binding** — a factory plus per-member hooks on the handle it returns:

```cpp
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type&, const char* name, const char* doc,
                           std::index_sequence<I...>);        // Bases[I] spliced
    static void add_default_ctor(auto& cls);
    template <std::meta::info Ctor>          static void add_constructor(auto& cls);
    template <class T>                       static void add_aggregate_constructor(auto& cls);
    template <std::meta::info Mem, class Style> static void add_field(auto& cls);
    template <std::meta::info Fn,  class Style> static void add_method(auto& cls);
    template <std::meta::info Fn,  class Style> static void add_static_method(auto& cls);
    template <std::meta::info Fn>            static void add_operator(auto& cls);
    static consteval const char* special_method_name(std::meta::info op_fn);
        // your language's name for a member operator ("__add__", "__eq"),
        // or nullptr if you don't expose it — this drives add_operator eligibility
```

**Enum binding** and **namespace/module binding**:

```cpp
    template <class E> static auto make_enum(module_type&, const char* name,
                                             const char* doc);
    template <std::meta::info Enum, class Style> static void add_enumerator(auto& e);
    template <class E> static void finish_enum(auto& e);   // e.g. export unscoped values

    static auto open_module(module_type&);                  // -> a "session" (scratch state)
    static void set_module_doc(module_type&, const char* doc);
    template <std::meta::info Fn,  class Style>
    static void add_function(module_type&, const char* name = nullptr);
    template <std::meta::info Var, class Style>
    static void add_variable(module_type&, auto& session, const char* name = nullptr);
    static module_type add_submodule(module_type&, const char* name);
    static void close_module(module_type&, auto& session); // finalize the session
};
```

A *session* is backend scratch state obtained per (sub)module — an accumulator
for anything your framework wants batched (the LuaCATS rod gathers text there,
for instance). If you need none, return an empty struct.

### What the driver does for you — and what it expects back

Names for classes, enums and submodules arrive at your factories **pre-styled**:
the driver has already applied the injected [name style](naming.md) and any
`weld_as` override. The *member*-level hooks instead take a trailing `Style`
parameter and resolve their own name via
`welder::name_of<Mem, language, Style, welder::ent_kind::…>()` — one call that
applies the call-site override, `weld_as`, and the style hook in the right
precedence. Docstrings arrive the same way: `doc` for classes and modules is
handed to you as a `const char*`; for functions, fold the annotation set with
[`welder::function_docstring`](docstrings.md) under the doc style of your
choice (or ignore it — the sol2 rod does, Lua has no runtime docstring slot).

!!! note "The two factories are contract-by-documentation"
    `make_class` / `make_enum` return `auto`, and a rod's factory legitimately
    does real work in its body (the sol2 rod registers constructors inside
    `make_class`), so the concept cannot probe them without instantiating that
    body against a placeholder type. They are checked the first time the driver
    instantiates your rod over a real welded type — expect the errors there,
    not at the `static_assert`.

### A compiled reference

The repository keeps a minimal, framework-free rod that satisfies the whole
contract and is driven through every entry point as a CTest —
[`tests/core/rod_probe.cpp`](https://github.com/skarndev/welder/blob/main/tests/core/rod_probe.cpp).
It is the fastest starting skeleton: copy it, replace the no-op bodies with
your framework's calls, and keep it compiling. For real-world reference
implementations, the shipped rods are ranked roughly by size: the LuaCATS rod
(text emission, no runtime), LuaBridge3, sol2, then the two Python rods.

A rod does not have to target a language *runtime* at all: the LuaCATS rod
emits a stub file and the trampolines rod emits a C++ header, both driven by
the same traversal. If your "framework" is a text format, `module_type` can be
a writer over an output stream.

Out of tree, all it takes is a header and a target that links
`welder::headers`; your rod struct can live in any namespace of yours —
`welder::rods::…` is a convention of the shipped rods, not a requirement of
the concept.

### Binding a new language

A rod for a language welder doesn't ship needs one more thing: a language
*identity* for the annotations to name. The `lang` value space is open for
exactly this — welder reserves bit indices 0–15 of the language mask, and
`welder::user_lang<Slot>` mints identities from the user range (16–31),
compile-time-checked so they can never collide with a shipped language or
overflow the mask:

```cpp
inline constexpr welder::lang ruby{welder::user_lang<0>};

struct [[=welder::weld(ruby)]] Gem { /* … */ };
```

A user language is a first-class `lang`: it works in `weld`, the per-language
`mark`s, `trust_bindable` and `weld_as` alike, and the core resolves it under
the same [resolution rule](annotations.md#the-resolution-rule).

Two conventions keep it sound:

- **One constant.** Mint the identity once — in the application that owns the
  binding — and spell both the annotations and the rod's language from that
  single constant, so the two can never disagree (an annotation naming one bit
  while the rod reads another would simply bind nothing).
- **Make the rod's language injectable.** A *published* rod shouldn't hardcode
  its slot; take it as a template parameter with a default, so an application
  combining two third-party rods that happened to pick the same slot can
  re-point one at instantiation:

```cpp
template <welder::lang Language = welder::user_lang<0>>
struct rod {
    static constexpr welder::lang language{Language};
    // … emission primitives …
};

// the app owns the slot assignment — two third-party rods, disjoint slots:
using ruby_rod = rods_ruby::rod<welder::user_lang<0>>;
using r_rod    = rods_r::rod<welder::user_lang<1>>;
```

These semantics are locked by the compile-time test
[`tests/core/user_lang.cpp`](https://github.com/skarndev/welder/blob/main/tests/core/user_lang.cpp).

## Custom traversal: resolutions and carriages

The traversal driver — the **carriage** — is itself a policy template,
`welder::carriages::basic_carriage<Resolution>`, injected as
`welder::welder`'s third template argument. The `Resolution` decides *which*
entities participate; the carriage body owns *how* they are walked and
emitted; the [bindability gate](bindability.md) stays enforced in either case.
Two resolutions ship, with their carriage aliases:

| Resolution | Carriage alias | Behavior |
|---|---|---|
| `welder::carriages::marker_resolution` | `welder::stitch_welding_carriage` (default) | Bind only where `weld` / `policy` / marks direct |
| `welder::carriages::greedy_resolution` | `welder::tack_welding_carriage` | Bind an unmarked library greedily (marks still prune) |

A bespoke resolution is a stateless struct satisfying the `welder::resolution`
concept — four `consteval` predicates (`participates`, `is_native_base`,
`member_participates`, `namespace_participates`) plus the `native_bases<T, L>`
hook. Since the shipped resolutions are ordinary structs, delegation is plain
inheritance. For example, tack-welding a third-party library while skipping
its underscore-prefixed internals:

```cpp
struct skip_internal : welder::carriages::greedy_resolution {
    static consteval bool member_participates(std::meta::info mem, welder::lang L,
                                              welder::policy_kind pol) {
        if (std::meta::has_identifier(mem) &&
            std::meta::identifier_of(mem).starts_with("_"))
            return false;
        return welder::carriages::greedy_resolution::member_participates(mem, L, pol);
    }
};

using my_carriage = welder::carriages::basic_carriage<skip_internal>;
welder::welder<rod<>, pep8, my_carriage>::weld_namespace<^^thirdparty>(m);
```

The carriage's members cross-reference each other (a namespace's classes bind
via `bind_type`, nested namespaces recurse via `bind_namespace`), so a
replacement *carriage* — as opposed to a resolution — is expected to be a
coherent whole rather than a partial override. In practice the resolution seam
is the one you want.

## Composing bespoke entry points

Every `weld_*` entry point is a one-line static forward to the carriage, so
`welder::welder` can be subclassed to package a recurring routine — the same
gated building blocks, your orchestration:

```cpp
struct my_welder : welder::welder<welder::rods::pybind11::rod<>,
                                  welder::rods::python::pep8> {
    static void weld_geometry(module_type& m) {
        weld_type<Vec2>(m);
        weld_type<Mesh>(m);
        weld_namespace_as_submodule<^^geo::detail_math>(m, "math");
    }
};
```

## Stability

These seams — the `welder::rod` and `welder::resolution` concepts,
`welder::carriages::basic_carriage`, the open language value space
(`welder::user_lang`), and subclassing `welder::welder` — are welder's
*supported* extension surface: out-of-tree code should need nothing from
`welder::detail`. Pre-1.0 the hook signatures may still evolve (see the
status note in the [README](https://github.com/skarndev/welder#readme));
changes will be called out per release.

Next: the [Languages](../backends/index.md) section shows what the shipped
rods do with this contract in practice.