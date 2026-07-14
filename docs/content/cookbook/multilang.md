# 07 — One library, two languages

*Source: [`examples/cookbook/07-multilang`][src].*

The flagship recipe: **one annotated header** (`journal.hpp`) shipped to Python
*and* Lua, each side idiomatic — different naming conventions, different names
for the same entity where idiom demands it, and even different *implementations*
of one concept per language. Plus both stub kinds, so editors get types and docs
in both worlds. (See also [Shipping to multiple languages](../backends/multiple.md).)

Three rods, **two languages**, one welded surface — the Lua module is built twice
(sol2 *and* LuaBridge3; on Windows CI, where sol2 has no Lua 5.4, LuaBridge3
carries the Lua side alone on 5.5) and the **same `check.lua` asserts both**:

| | Python | Lua |
|---|---|---|
| rod | nanobind (`lang::py` — welder never distinguishes backends within a language) | sol2 **and** LuaBridge3 (`lang::lua`, same reason) |
| name style | `welder::rods::python::pep8` | `welder::naming::snake_case` |
| `Entry::renderLine` | `Entry.render_line` | `journal.entry:render_line` |
| docstrings | Google-style `__doc__` | the LuaCATS `---@meta` stub |
| stubs | `.pyi` via nanobind's bundled stubgen | `welder_luacats_generate_stub()` |

## A different name per language

`weld_as` is per-language and verbatim — one C++ entity, spelled to each
language's taste ([Naming conventions](../guide/naming.md)):

```cpp
[[=welder::weld_as(welder::lang::py, "to_text"),
  =welder::weld_as(welder::lang::lua, "as_string"),
  =welder::doc("Render every entry, one per line.")]]
std::string renderAll() const;
```

## Language-flavored members: `mark::only`

"Save this somewhere" means a *file-like object* to a Python programmer and a
*writer callback* to a Lua one. The recipe implements the concept twice as
distinct C++ methods, gates each to its **language** with `mark::only`, and
surfaces both under the **same public name**:

```cpp
//  - Python: any object with .write — open(), io.StringIO, ...
[[=welder::mark::only(welder::lang::py), =welder::weld_as("save_to"),
  =welder::doc("Write the rendered journal to a file-like object.")]]
void saveToFileLike(nanobind::object file) const {
    file.attr("write")(nanobind::str(renderAll().c_str()));
}

//  - Lua: a writer callback — framework-NEUTRAL on purpose (1)
[[=welder::mark::only(welder::lang::lua),
  =welder::mark::trust_bindable(welder::lang::lua), // for the LuaCATS stub rod (2)
  =welder::weld_as("save_to"),
  =welder::doc("Pass each rendered line to a writer function.")]]
void saveToWriter(const std::function<void(const std::string&)>& write) const {
    for (const auto& e : entries_) { write(e.renderLine()); }
}
```

1.  `mark::only` scopes to a *language*, and `lang::lua` is served by **two rods**
    here — a `sol::protected_function` parameter would lock the method to sol2.
    The neutral `std::function` binds under both.
2.  The LuaCATS *stub* rod has no Lua framework whose casters it could consult —
    [`mark::trust_bindable`](../guide/trust-casters.md) vouches for the signature.

Because each method is `only`-gated, the rods of the *other* language never bind
— or even inspect — it. The nanobind header is included by every TU so the class
definition stays identical; an inline member a TU never uses is not emitted, so
the Lua modules carry no Python code.

sol2 converts `std::function` arguments out of the box; LuaBridge3 does not — so
its TU teaches it with a ~20-line `luabridge::Stack` specialization (the same
extension point LuaBridge3's own `Vector.h` uses), wrapping the Lua function in a
registry-anchored `LuaRef`. The pattern to copy: **framework glue lives in that
framework's TU; the shared header stays neutral.**

## The entry points

Each side is one `WELDER_MODULE` line — the optional third argument is the exact
`welder::welder<…>` to drive the weld with, which is how a name style threads
through the one-line module form:

=== ":simple-python: Python (nanobind)"

    ```cpp
    WELDER_MODULE(journal, nanobind,
                  welder::welder<welder::rods::nanobind::rod<>,
                                 welder::rods::python::pep8>) {}
    ```

=== ":simple-lua: Lua (sol2)"

    ```cpp
    WELDER_MODULE(journal, sol2,
                  welder::welder<welder::rods::sol2::rod,
                                 welder::naming::snake_case>) {}
    ```

=== ":simple-lua: Lua (LuaBridge3)"

    ```cpp
    // same language, same style, different framework — only the selector differs
    WELDER_MODULE(journal, luabridge,
                  welder::welder<welder::rods::luabridge::rod,
                                 welder::naming::snake_case>) {}
    ```

=== ":simple-lua: LuaCATS stub generator"

    ```cpp
    int main(int argc, char** argv) {
        std::ofstream out{argv[1]};
        // the SAME style as the Lua bindings, so the stub matches name-for-name
        welder::rods::luacats::rod::generate<^^journal,
                                             welder::naming::snake_case>(out);
    }
    ```

Both Lua modules answer `require("journal")` (each `.so` is built into its own
directory; the check runs once per rod with the matching `LUA_CPATH`).

## What the checks assert

`check.py` and `check.lua` walk the same surface side by side — and `check.lua`
runs **twice**, once per Lua rod: styled names (`add_entry` both sides, but
`Notebook` vs `notebook`), the per-language `to_text`/`as_string` split,
`save_to` fed an `io.StringIO` in Python and a closure in Lua, docstrings in
`__doc__`, and both generated stubs containing the styled declarations (the
LuaCATS stub carries the doc text Lua drops at runtime).

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/07-multilang