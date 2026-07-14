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
- **The namespace walker skips templates.** `weld_namespace` cannot enumerate what
  does not exist — an uninstantiated template is not a bindable entity, so it stays
  skipped. Instantiations you want exposed are welded explicitly, one `weld_type`
  each.

## A worked example

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

### The name is not optional

For a plain class, `weld_type<T>(m)` defaults the target name to `T`'s C++
identifier. A template specialization **has no identifier** — reflection's
`has_identifier(^^Box<int>)` is `false`, and `Box<int>` wouldn't be a valid
Python or Lua identifier anyway; welder does not invent a spelling. So you pass
the trailing name override: it is used **verbatim** (it bypasses the
[name style](naming.md) and beats `weld_as`). Omitting it — with no `weld_as` to
fall back on — fails at binding time with a message pointing you here.

!!! info "Why not `weld_as` on the template?"

    A `weld_as` on the primary template rides on *every* instantiation alike — it
    cannot tell `Box<int>` from `Box<double>`, so all instantiations would claim
    the same target name. The per-call name override is the per-instantiation
    tool.

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