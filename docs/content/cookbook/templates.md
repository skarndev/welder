# 06 — Binding template instantiations

*Source: [`examples/cookbook/06-templates`][src].*

welder's model for templates is **annotate the template, bind instantiations**
([Binding templates](../guide/templates.md)). The annotations on the primary
template are carried by every instantiation; you pick the concrete types. An
instantiation has no identifier of its own, so it needs a name from you — and
there are two routes: a **namespace-scope alias** riding the sweep (recommended —
the alias *is* the name), or a directly-welded instantiation with an explicit
name string.

## Annotate once

```cpp
template <class T>
struct
[[
  =welder::weld(welder::lang::py),
  =welder::doc("A single value in a labelled box."),
  =welder::tparam("T", "the stored value type")
]]
Box {
    std::string label;
    T value{};
    ...
};

template <class T>
[[=welder::weld(welder::lang::py), =welder::doc("Swap the contents of two boxes.")]]
void swap_boxes(Box<T>& a, Box<T>& b);
```

## Route 1 — alias the instantiation, let the sweep bind it

`members_of(ns)` enumerates the class *template*, never a specialization, so a
namespace-scope alias is how an instantiation enters a `weld_namespace` sweep —
binding under the alias's name, with the template's `weld`/`doc` gating and
documenting it:

```cpp
namespace boxes {
using IntBox = Box<int>;          // ← binds as IntBox
using TextBox = Box<std::string>; // ← binds as TextBox
}

weld::weld_namespace<^^boxes>(m); // no name strings anywhere
```

The alias may additionally carry `weld` (the opt-in for a third-party template
you cannot annotate) or `weld_as` (a verbatim rename); both take precedence over
the template's. Every other mark belongs on the template — see the
[guide](../guide/templates.md#welding-through-an-alias-the-namespace-sweep) for
the full rules (duplicates and plain-type aliases are compile errors).

## Route 2 — weld the instantiation directly

```cpp
weld::weld_type<boxes::Box<double>>(m, "RealBox");

// A function-template instantiation is reflected with substitute():
weld::weld_function<std::meta::substitute(^^boxes::swap_boxes, {^^int})>(
    m, "swap_int_boxes");
```

!!! note "Why the explicit name here"

    `has_identifier(^^Box<double>)` is false — there is no C++ spelling to derive
    a name from, and Python wouldn't accept `Box<double>` anyway. An alias cannot
    help on this route either: a type template parameter *dealiases*, so by the
    time `weld_type` sees the type the alias is gone. The call-site name (or a
    `weld_as` on the template) is therefore the name; omitting both fails with a
    clear message at binding time.

## What the check asserts

The alias-bound instantiations work independently (`IntBox` holds an `int`,
`TextBox` a `str`) and the bare template `Box` is absent; the directly-welded
`RealBox` works under its verbatim name; the primary template's `doc` shows up on
*all three* classes' `__doc__`; and the `substitute()`-formed `swap_int_boxes`
really swaps.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/06-templates