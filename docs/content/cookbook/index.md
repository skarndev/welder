# Cookbook

Small, complete, **runnable** recipes — one per page, each a standalone consumer
project under [`examples/cookbook/`][src] and asserted by a CTest in CI. Where the
[guide](../guide/index.md) explains each feature in isolation, a recipe shows the
whole dish: the annotated C++, the CMake that builds it, and a check script that
loads the module and proves what bound.

<div class="grid cards" markdown>

-   **[01 — One of everything](hello.md)** — weld a type, an enum, a free function
    and a namespace variable, one `weld_*` call each.
-   **[02 — Discovery rules](discovery.md)** — policies, marks, nested namespaces,
    pruning `detail`, `weld_as` renames, `WELDER_MODULE`.
-   **[03 — Inheritance](inheritance.md)** — welded bases become native bases;
    unwelded bases flatten in as mixins.
-   **[04 — Virtual methods](virtuals.md)** — Python subclasses overriding C++
    virtuals through hand-written trampolines.
-   **[05 — Generated trampolines](generated-trampolines.md)** — the same, with the
    trampolines reflection-generated at build time.
-   **[06 — Templates](templates.md)** — weld class/function template
    instantiations under explicit names.
-   **[07 — One library, two languages](multilang.md)** — Python (nanobind) + Lua
    (sol2 *and* LuaBridge3) from one header: name styles, per-language `weld_as`,
    `mark::only` language flavors, `.pyi` + LuaCATS stubs.
-   **[08 — Tack welding](tack-welding.md)** — greedily bind a third-party library
    that carries no welder annotations at all.
-   **[09 — Custom traversal](custom-traversal.md)** — subclass the tack
    resolution to honor a library's privacy convention: prune `detail`
    namespaces and `_underscore` internals.

</div>

## Building the recipes

The cookbook is a **standalone super-project**: it obtains welder with
`FetchContent` — exactly the way [a consumer would](../guide/getting-started.md#consuming-welder)
— and FetchContent-pins the backends (pybind11, nanobind, sol2). No Conan, nothing
preinstalled beyond gcc-16, CMake + Ninja, a Python, and (for the Lua half of
recipe 07) a Lua install:

```bash
cmake -S examples/cookbook -B build/cookbook -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-16 \
  -DWELDER_LUA_DIR="$(brew --prefix lua@5.4)"   # optional: enables the Lua recipe
cmake --build build/cookbook
ctest --test-dir build/cookbook --output-on-failure
```

By default welder itself is fetched from GitHub. welder's CI instead points the
fetch at the current checkout (`-DFETCHCONTENT_SOURCE_DIR_WELDER=$PWD`), so every
recipe — and the FetchContent consumption path itself — is exercised on every
commit.

!!! tip "Copying a recipe"

    Each recipe directory is deliberately self-contained (one `CMakeLists.txt`,
    one or a few `.cpp`, a check script). To start your own project from one,
    copy the recipe directory plus the dependency block of the top-level
    [`CMakeLists.txt`][src] — that block *is* the "consume welder without Conan"
    reference.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook