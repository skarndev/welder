# The welder cookbook

Small, self-contained consumer projects, one per recipe — each demonstrating a
slice of welder end to end and asserted by a CTest. The narrative walkthroughs
live in the documentation's **Cookbook** section
(`docs/content/cookbook/`, published at <https://skarndev.github.io/welder/cookbook/>).

Unlike `examples/{python_poc,lua_poc,…}`, this is a **standalone super-project**:
it obtains welder with `FetchContent` — the same way a real consumer would — so
building it also tests welder's packaging seams. CI builds it against the current
checkout by redirecting the fetch:

```bash
cmake -S examples/cookbook -B build/cookbook -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-16 \
  -DFETCHCONTENT_SOURCE_DIR_WELDER=$PWD \
  -DWELDER_LUA_DIR="$(brew --prefix lua@5.4)" \
  -DPython_EXECUTABLE="$(brew --prefix python@3.14)/bin/python3.14"
cmake --build build/cookbook
ctest --test-dir build/cookbook --output-on-failure
```

Omit `FETCHCONTENT_SOURCE_DIR_WELDER` to fetch welder from GitHub instead (what a
reader copying a recipe gets). Omit `WELDER_LUA_DIR` to skip the Lua half of
recipe 07 (or point `WELDER_LUABRIDGE_LUA_DIR` at a Lua 5.5 install — e.g. MSYS2
ucrt64, which is how Windows CI runs it with sol2 off). The backends (pybind11,
nanobind, sol2, LuaBridge3) are FetchContent-pinned — no Conan, nothing
preinstalled beyond gcc-16, CMake/Ninja, Python and (optionally) Lua.

| Recipe | Shows |
|---|---|
| `01-hello` | weld a type, an enum, a free function, a namespace variable — one `weld_*` call each |
| `02-discovery` | policies (`automatic`/`opt_in`), marks, nested namespaces & pruning, `weld_as`, `WELDER_MODULE` |
| `03-inheritance` | welded bases → native inheritance; unwelded bases → flattened mixins |
| `04-virtuals` | Python subclasses overriding C++ virtuals — hand-written trampolines, both discovery forms, `bind_flat` |
| `05-generated-trampolines` | the same, with the trampolines generated at build time (`welder_generate_trampolines`) |
| `06-templates` | welding class/function template instantiations under explicit names |
| `07-multilang` | one header → Python (nanobind) + Lua (sol2 **and** LuaBridge3, same checks): name styles, per-language `weld_as`, `mark::only` language flavors, `.pyi` + LuaCATS stubs |
| `08-tack-welding` | greedily binding an unannotated third-party library (`tack_welding_carriage`) |
| `09-custom-traversal` | a bespoke resolution: tack welding that prunes `detail` namespaces + `_underscore` internals |
| `10-containers` | opaque, reference-semantic `std::vector` members via the generator: live mutation, `by_value` opt-out, zero-copy NumPy (scalars + POD-struct structured arrays) |