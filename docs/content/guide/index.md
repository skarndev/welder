# Guide

This guide walks through welder from the ground up: installing the toolchain,
the annotation vocabulary, and each binding feature with runnable examples.

<div class="grid cards" markdown>

-   **[Getting started](getting-started.md)** — toolchain, build, your first module.
-   **[Annotation vocabulary](annotations.md)** — `weld`, `policy`, `mark`, and how resolution works.
-   **[Binding a type](binding-types.md)** — fields, constructors, methods, operators.
-   **[Enums](enums.md)** — scoped/unscoped enums and per-enumerator marks.
-   **[Inheritance](inheritance.md)** — welded bases vs. flattened mixins.
-   **[Namespaces & modules](namespaces-modules.md)** — bind a whole namespace or module in one line.
-   **[Docstrings](docstrings.md)** — `doc` / `returns` / `tparam` flowing to `__doc__` and the C++ docs.
-   **[The bindability gate](bindability.md)** — why an unrepresentable type is a hard error.
-   **[Trust & type casters](trust-casters.md)** — escape hatches for types welder can't see.
-   **[Generating C++ docs](cpp-docs.md)** — the Doxygen INPUT_FILTER that reads the same annotations.

</div>

!!! tip "Read it in any language"

    Every feature here is **backend-agnostic** — the same annotated C++ binds to
    Python and Lua alike. Examples are shown per language in tabs; pick your tab and
    the whole guide follows it. When you're ready to choose or combine backends, the
    [Backends](../backends/index.md) section covers each one and how to
    [ship several from one build](../backends/multiple.md).

## The mental model

welder splits into a language-agnostic **core** and pluggable **backends**:

- The **core** does all the reflection — reading your annotations, deciding
  *which* members bind per language, checking each type is *representable*, and
  walking types / namespaces / bases.
- A **backend** (e.g. `welder::pybind11`) is a stateless struct that supplies only
  the *emission primitives*: how to register a class, a method, a property in its
  framework. It never re-implements the traversal or the annotation semantics.

Everything you write lives in the annotations; the two consumption forms are
equivalent:

=== "Module (`import welder;`)"

    ```cpp
    import welder;
    #include <pybind11/pybind11.h>
    #include <welder/backends/python/pybind11/backend.hpp>
    ```

=== "Header-only"

    ```cpp
    #include <welder/welder.hpp>
    #include <pybind11/pybind11.h>
    #include <welder/backends/python/pybind11/backend.hpp>
    ```

!!! note "Order matters"

    The vocabulary must arrive **first** — via `import welder;` *or*
    `#include <welder/welder.hpp>` — *then* the backend header. The backend
    (which pulls in `<meta>` and pybind11) deliberately does not redeclare the
    vocabulary. See [the module/header boundary](../architecture.md#the-module-vs-header-boundary).
