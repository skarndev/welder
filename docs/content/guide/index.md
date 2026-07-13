# Guide

This guide walks through welder from the ground up: installing the toolchain,
the annotation vocabulary, and each binding feature with runnable examples.

<div class="grid cards" markdown>

-   **[Getting started](getting-started.md)** — toolchain, build, your first module.
-   **[Annotation vocabulary](annotations.md)** — `weld`, `policy`, `mark`, and how resolution works.
-   **[Binding a type](binding-types.md)** — fields, constructors, methods, operators.
-   **[Binding templates](templates.md)** — annotate the template, bind instantiations.
-   **[Enums](enums.md)** — scoped/unscoped enums and per-enumerator marks.
-   **[Inheritance](inheritance.md)** — welded bases vs. flattened mixins.
-   **[Namespaces & modules](namespaces-modules.md)** — bind a whole namespace or module in one line.
-   **[Docstrings](docstrings.md)** — `doc` / `returns` / `tparam` flowing to `__doc__` and the C++ docs.
-   **[The bindability gate](bindability.md)** — why an unrepresentable type is a hard error.
-   **[Trust & type casters](trust-casters.md)** — escape hatches for types welder can't see.
-   **[Generating C++ docs](cpp-docs.md)** — the Doxygen INPUT_FILTER that reads the same annotations.
-   **[Extending welder](extending.md)** — write your own rod, resolution, or entry point.
-   **[Troubleshooting & FAQ](faq.md)** — the common errors and their fixes.

</div>

!!! tip "Read it in any language"

    Every feature here is **backend-agnostic** — the same annotated C++ binds to
    Python and Lua alike. Examples are shown per language in tabs; pick your tab and
    the whole guide follows it. When you're ready to choose or combine rods, the
    [Languages](../backends/index.md) section covers each one and how to
    [ship several from one build](../backends/multiple.md).

## The mental model

welder splits into a language-agnostic **core** and pluggable **rods** (a rod is a
welding rod — the backend that lays a framework's bindings down):

- The **core** does all the reflection — reading your annotations, deciding
  *which* members bind per language, checking each type is *representable*, and
  walking types / namespaces / bases.
- A **rod** (`welder::rods::pybind11::rod<>`, …) is a stateless struct that supplies
  only the *emission primitives*: how to register a class, a method, a property in
  its framework. It never re-implements the traversal or the annotation semantics.
  You drive it through one entry point, `welder::welder<Rod>`.

Everything you write lives in the annotations. welder ships **header-only**, so a
consuming TU brings the vocabulary in with a single include:

```cpp
#include <welder/vocabulary.hpp>
#include <pybind11/pybind11.h>
#include <welder/rods/python/pybind11/rod.hpp>
```

!!! note "Order matters"

    The vocabulary must arrive **first** — `#include <welder/vocabulary.hpp>` —
    *then* the rod header. The rod header (which pulls in `<meta>` and pybind11)
    deliberately does not redeclare the vocabulary.

!!! info "Header-only, for now"

    An `import welder;` C++20 module wrapper is planned but currently deferred —
    see [Header-only for now](../header-only.md) for the toolchain reasons why.
