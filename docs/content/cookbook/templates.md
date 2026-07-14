# 06 — Binding template instantiations

*Source: [`examples/cookbook/06-templates`][src].*

welder's model for templates is **annotate the template, bind instantiations**
([Binding templates](../guide/templates.md)). The annotations on the primary
template are carried by every instantiation; you pick the concrete types — and
the names, because an instantiation has no identifier of its own, so the explicit
`weld_type` name is *required*.

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

## Bind each instantiation

```cpp
using weld = welder::welder<welder::rods::pybind11::rod<>>;
weld::weld_type<boxes::Box<int>>(m, "IntBox");
weld::weld_type<boxes::Box<std::string>>(m, "TextBox");

// A function-template instantiation is reflected with substitute():
weld::weld_function<std::meta::substitute(^^boxes::swap_boxes, {^^int})>(
    m, "swap_int_boxes");
```

!!! note "Why the explicit name"

    `has_identifier(^^Box<int>)` is false — there is no C++ spelling to derive a
    name from, and Python wouldn't accept `Box<int>` anyway. The call-site name
    (or a `weld_as` on the template) is therefore the name. Omitting both fails
    with a clear message at binding time.

## What the check asserts

Both instantiations work independently (`IntBox` holds an `int`, `TextBox` a
`str`); the primary template's `doc` shows up on *both* classes' `__doc__`; and
the `substitute()`-formed `swap_int_boxes` really swaps.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/06-templates