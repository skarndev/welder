# Binding templates

A class template is not a type — until you instantiate it, there is nothing to
bind. C++26 reflection enforces this literally: `annotations_of` refuses an
*uninstantiated* template (or concept), so welder cannot read your
[vocabulary](annotations.md) off `Box` itself. But nothing is lost: annotations on
a template **declaration** are carried by every **instantiation**, so welder's
model is simple — **annotate the template, bind instantiations.**

!!! example "In the cookbook"

    [Recipe 06 — Binding template instantiations](../cookbook/templates.md) is
    this page as a runnable module, including a `substitute()`-formed
    function-template instantiation.

## The model

Two consequences follow directly:

- **You hand welder instantiations.** `weld_type<Box<int>>(m, "BoxInt")` is
  legitimate: the `weld`, `policy`, marks and docs on the template resolve through
  the instantiation exactly as on a plain class.
- **The namespace walker skips templates** — an uninstantiated template is not a
  bindable entity. But an instantiation *can* ride the sweep: declare a
  **namespace-scope alias** for it, and `weld_namespace` binds it under the
  alias's name. That is the recommended route — see
  [Welding through an alias](#welding-through-an-alias-the-namespace-sweep) below.

## Welding through an alias (the namespace sweep)

`members_of(ns)` enumerates the class *template*, never an instantiation — so a
namespace-scope alias is the way one enters a `weld_namespace` sweep. The alias
supplies everything a specialization otherwise lacks: a C++ **identifier** (which
text-emitting rods like the [trampoline generator](inheritance.md#generating-trampolines-automatically)
need to spell the type) and the default **target-language name** — no stringified
name anywhere:

```cpp
namespace shapes {

template <class T>
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]] Box {
    T value{};
};

using IntBox = Box<int>;        // ← binds as `IntBox`, py and lua
using WordBox = Box<std::string>; // a second instantiation, its own name

} // namespace shapes

// weld_namespace<^^shapes>(m) now binds IntBox and WordBox like any welded type.
```

The rules, all enforced at compile time:

- **The template's `weld` stays effective** — it is read through the
  instantiation, so annotating the template once covers every aliased
  instantiation. Marks, `policy`, and docs resolve the same way.
- **The alias may carry `weld` and `weld_as` — nothing else.** Both *take
  precedence* over the template's when present. An alias-level `weld` is the
  opt-in for a **third-party template you cannot annotate**
  (`using VBuf [[=welder::weld(welder::lang::py)]] = vendor::Buf<int>;`) — note it
  *replaces* the template's language set rather than adding to it. Any other
  welder mark on an alias is a compile error pointing you at the template.
- **One alias per specialization.** Two participating aliases naming the same
  instantiation would register it twice; welder diagnoses the duplicate at
  compile time.
- **Plain-type aliases don't bind.** `using Alias = SomeWeldedClass;` would
  register the class a second time, so a welded target makes the alias a hard
  error — rename with `weld_as` instead.
- **Nested types come along.** A [nested type](binding-types.md#nested-types)
  declared in the template resolves off the *instantiation* like any member and
  binds under the alias's name — `IntSilo.Hatch`, `IntSilo.State`. With the
  `weld` on the template, members whose signatures use them pass the gate as
  usual. For an alias-*opt-in* (third-party) template the nested types still
  register, but a signature naming one — or the instantiation itself — needs a
  [`trust_bindable` hatch](trust-casters.md): the gate's registration oracle is
  a pure predicate of the declaration and cannot see a weld that lives on a
  namespace-scope alias.

## Annotate once, weld each instantiation

Annotate the template once; weld as many instantiations as you need:

```cpp
#include <welder/vocabulary.hpp>

template <class T>
struct
[[
  =welder::weld(welder::lang::py),
  =welder::doc("A box holding one value."),
  =welder::tparam("T", "the stored element type")
]]
Box {
    [[=welder::doc("the stored value")]] T value;

    [[=welder::doc("Get the value, scaled."), =welder::returns("value times k")]]
    T get([[=welder::doc("a scale factor")]] int k) const { return value * k; }
};
```

```cpp
PYBIND11_MODULE(mymod, m) {
    using weld = welder::welder<welder::rods::pybind11::rod<>>;
    weld::weld_type<Box<int>>(m, "BoxInt");
    weld::weld_type<Box<double>>(m, "BoxDouble");
}
```

```pycon
>>> b = mymod.BoxInt()
>>> b.value = 21
>>> b.get(2)
42
```

### The name is not optional (on the direct route)

For a plain class, `weld_type<T>(m)` defaults the target name to `T`'s C++
identifier. A template specialization **has no identifier** — reflection's
`has_identifier(^^Box<int>)` is `false`, and `Box<int>` wouldn't be a valid
Python or Lua identifier anyway; welder does not invent a spelling. So you pass
the trailing name override: it is used **verbatim** (it bypasses the
[name style](naming.md) and beats `weld_as`). Omitting it — with no `weld_as` to
fall back on — fails at binding time with a message pointing you here.

This is also why the direct route cannot see an alias: a type template parameter
*dealiases* — by the time `weld_type<shapes::IntBox>` reaches welder, `IntBox` is
`Box<int>` and the alias is gone. The [namespace sweep](#welding-through-an-alias-the-namespace-sweep)
sees the alias *declaration* itself, which is what makes the automatic naming
work there.

!!! info "Why not `weld_as` on the template?"

    A `weld_as` on the primary template rides on *every* instantiation alike — it
    cannot tell `Box<int>` from `Box<double>`, so all instantiations would claim
    the same target name. The per-instantiation tools are the alias's name (or a
    `weld_as` *on the alias*) on the sweep route, and the per-call name override
    on the direct route. (With a *single* aliased instantiation, a template-level
    `weld_as` does apply — the alias simply inherits it when it has none of its
    own.)

## Which declaration's annotations win

An instantiation carries the annotations of the declaration that **governs** it:

| Instantiation | Governed by | Whose annotations it carries |
|---|---|---|
| `Box<double>` | the primary template | the primary's |
| `Box<char*>` | a partial specialization `Box<T*>` | the partial specialization's |
| `Box<int>` | an explicit specialization `Box<int>` | the explicit specialization's |

```cpp
template <class T>
struct [[=welder::doc("Primary Box.")]] Box { T v; };

template <>
struct [[=welder::doc("Box of int (explicit specialization).")]] Box<int> { int v; };

template <class T>
struct [[=welder::doc("Box of pointer (partial specialization).")]] Box<T*> { T* v; };
```

`Box<double>` reads back `"Primary Box."`; `Box<int>` and `Box<char*>` each read
back their specialization's own doc. There is no merging — a specialization is a
separate declaration and brings its own annotations (including its own members and
their marks).

This holds for the **whole vocabulary**, not just docs: `weld`, `policy` and the
per-member `mark`s resolve through instantiations under the same
[resolution rule](annotations.md#the-resolution-rule) as on a plain class —

```cpp
template <class T>
struct [[=welder::weld(welder::lang::py)]] Welded {
    T value;                            // bound
    [[=welder::mark::exclude]] T hidden;  // not bound, in any instantiation
};
```

— and member annotations *inside* a class-template instantiation resolve too:
field docs, method `doc`/`returns`, and parameter docs all read back off
`Box<double>` exactly as written on the template.

!!! note "Function and variable templates"

    The same carrying applies beyond classes: a function-template instantiation
    carries the template's summary, `returns` and parameter docs, and a
    variable-template instantiation carries its `doc`. These semantics are locked
    in by compile-time `static_assert`s in `tests/core/template_annotations.cpp`.

## Member function templates

A member function template is skipped by the member walk, silently — at the
reflection level it is a *template*, not a function, and welder cannot invent the
type arguments a binding would need. Marks on it are inert, and welder cannot
diagnose them either: C++26 reflection refuses `annotations_of` on an
*uninstantiated* template (the same restriction behind this whole page), so the
mark is unreadable until an instantiation exists. This is the "annotate the
template, bind instantiations" model in miniature, with two practical
consequences:

**Chaining is the route.** There is no `weld_type`-style entry for a member
template, but `weld_type` returns the rod's native class handle precisely so you
can add what welder shouldn't guess:

```cpp
struct [[=welder::weld(welder::lang::py)]] Mixer {
    std::string mix(int x) const;                 // welder binds these two
    std::string mix(const std::string& s) const;  // as one overload group
    template <class T> std::string mix(T v) const; // welder skips this
};

auto cls{weld::weld_type<Mixer>(m)};
cls.def("mix", &Mixer::mix<double>);   // pybind11: joins the overload set
```

On the Python rods the chained instantiation **joins the welder-bound overload
set** — pybind11 and nanobind merge same-named defs into one overloaded function,
and exact matches win across all overloads, so registration order doesn't shadow
anything. (Chain under the name welder actually bound — a
[name style](naming.md) or `weld_as` rename applies.) The Lua frameworks instead
*replace* same-key registrations, so this pattern is Python-only; on sol2 /
LuaBridge3 register the full overload set by hand in one go.

**Mixed names close the `substitute` door.** The
`weld_function<std::meta::substitute(^^fn, {^^int})>` route (below) requires
`^^fn` to denote the template *uniquely*. If non-template overloads share the
name, `^^fn` names an overload set — ill-formed, there is no overload-set
reflection — so neither the template nor its instantiations can be spelled
through reflection at all. Plain C++ has no such trouble: `&Mixer::mix<double>`
disambiguates by the explicit template arguments, which is exactly what the
chaining route uses.

## `tparam` — documenting template parameters

A template parameter is not a reflectable entity, so its doc rides on the
template itself — `tparam("name", "text")`, repeatable and **ordered**:

```cpp
template <class K, class V>
struct
[[
  =welder::doc("A dictionary."),
  =welder::tparam("K", "the key type"),
  =welder::tparam("V", "the mapped type")
]]
Dict { /* … */ };
```

In the [C++ reference](cpp-docs.md) each becomes an `@tparam` line. On the
reflection side they are read back **off an instantiation** via
`welder::tparam_docs<Ent>()`, which returns the name/text pairs in declaration
order (and an empty array for an entity with no `tparam` annotations) — the hook
rod docstrings read from. See [Docstrings](docstrings.md#tparam-documenting-templates)
for the rest of the doc vocabulary.

## One annotation, two audiences

Templates are where the dedupe story earns its keep. The
[Doxygen filter](cpp-docs.md) is **textual**, so annotations inside a template
translate like anywhere else — it doesn't care that reflection cannot read an
uninstantiated template. The same annotation therefore feeds both paths:

- the **C++ API reference**, textually, off the template declaration itself;
- every **bound instantiation's runtime docstring**, via instantiation
  reflection.

You write the doc once, on the template; `Box<int>` and `Box<double>` each carry
it into their `__doc__`, and the reference documents `Box` — no shadow copy to
keep in sync.