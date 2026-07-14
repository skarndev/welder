# 08 — Tack welding a third-party library

*Source: [`examples/cookbook/08-tack-welding`][src].*

Everything so far assumed you can annotate the C++. When you can't — the types
come from a library you don't control — swap the **carriage**: welder's traversal
driver is the third template argument of `welder::welder`, and
`welder::tack_welding_carriage` ignores the missing `weld` markers, binding
**greedily** ([Extending welder](../guide/extending.md) has the carriage model).

## The "vendor" library

`vecmath.hpp` is deliberately plain C++ — structs, operators, free functions,
constants, a nested namespace, and **zero welder annotations**.

## The binding TU

```cpp
#include "vecmath.hpp" // the "third-party" header — zero welder annotations

PYBIND11_MODULE(fastvec, m) {
    using tack = welder::welder<welder::rods::pybind11::rod<>,
                                welder::naming::none,
                                welder::tack_welding_carriage>;
    tack::weld_namespace<^^vecmath>(m);
}
```

Under tack welding every reflectable type/function/variable participates, nested
namespaces recurse greedily into submodules, and public bases are flattened.
Note `Vec3` appearing in the library's own signatures (`dot`, `cross`) with no
annotation and no hatch: the tack carriage's registration oracle accepts class
types its own greedy pass registers. Two things do **not** change:

- **The [bindability gate](../guide/bindability.md) still holds.** A genuinely
  non-representable type stays a compile error, as does a **forward-declared**
  (incomplete) type the walk cannot register; for a type registered elsewhere
  (hand-bound, another library's bindings), the
  [`trust_bindable` hatches](../guide/trust-casters.md) remain the escape valve.
- **Marks are still honored.** If the header does carry a `mark::exclude`
  (perhaps via a patch), it is respected.

!!! tip "Mixing stitch and tack"

    The tack welder is just another `welder::welder` alias — use the default
    (stitch) welder for your own annotated types and a tack welder for the
    vendor namespace, in the same module.

## What the check asserts

`Vec3` binds with its synthesized aggregate constructor, methods, and
`__add__`/`__eq__` operators; `cross` and `EPSILON` arrive as module members;
`vecmath::units` became the `fastvec.units` submodule — all without touching the
vendor header.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/08-tack-welding