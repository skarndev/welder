# 07 — One library, two languages

*Source: [`examples/cookbook/07-multilang`][src].*

The flagship recipe: **one annotated header** (`journal.hpp`) shipped to Python
*and* Lua, each side idiomatic — different naming conventions, different names
for the same entity where idiom demands it, and even different *implementations*
of one concept per language. Plus both stub kinds, so editors get types and docs
in both worlds. (See also [Shipping to multiple languages](../backends/multiple.md).)

Two rods, one welded surface:

| | Python | Lua |
|---|---|---|
| rod | nanobind (`lang::py` — welder never distinguishes Python backends) | sol2 (`lang::lua`) |
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

## Backend-flavored members: `mark::only`

"Save this somewhere" means a *file-like object* to a Python programmer and a
*writer callback* to a Lua one. The recipe implements the concept twice as
distinct C++ methods, gates each to its language with `mark::only`, and surfaces
both under the **same public name**:

```cpp
//  - Python: any object with .write — open(), io.StringIO, ...
[[=welder::mark::only(welder::lang::py), =welder::weld_as("save_to"),
  =welder::doc("Write the rendered journal to a file-like object.")]]
void saveToFileLike(nanobind::object file) const {
    file.attr("write")(nanobind::str(renderAll().c_str()));
}

//  - Lua: a writer callback.
[[=welder::mark::only(welder::lang::lua),
  =welder::mark::trust_bindable(welder::lang::lua), // for the LuaCATS stub rod (1)
  =welder::weld_as("save_to"),
  =welder::doc("Pass each rendered line to a writer function.")]]
void saveToWriter(sol::protected_function write) const {
    for (const auto& e : entries_) { write(e.renderLine()); }
}
```

1.  The sol2 rod converts `sol::protected_function` natively, but the LuaCATS
    *stub* rod deliberately has no sol2 dependency and cannot know that —
    [`mark::trust_bindable`](../guide/trust-casters.md) vouches for the signature.

Because each method is `only`-gated, the other rod never binds — or even
inspects — it. Both frameworks' headers are included by every TU so the class
definition stays identical; an inline member a TU never uses is not emitted, so
the Python module carries no Lua code and vice versa.

## The entry points

Each side is a handful of lines — same `weld_module`, different rod + style:

=== "Python (nanobind)"

    ```cpp
    NB_MODULE(journal, m) {
        using weld = welder::welder<welder::rods::nanobind::rod<>,
                                    welder::rods::python::pep8>;
        weld::weld_module<^^journal>(m);
    }
    ```

=== "Lua (sol2)"

    ```cpp
    extern "C" int luaopen_journal(lua_State* L) {
        sol::state_view lua{L};
        sol::table m{lua.create_table()};
        using weld = welder::welder<welder::rods::sol2::rod,
                                    welder::naming::snake_case>;
        weld::weld_module<^^journal>(m);
        return sol::stack::push(L, m);
    }
    ```

=== "LuaCATS stub generator"

    ```cpp
    int main(int argc, char** argv) {
        std::ofstream out{argv[1]};
        // the SAME style as the sol2 binding, so the stub matches name-for-name
        welder::rods::luacats::rod::generate<^^journal,
                                             welder::naming::snake_case>(out);
    }
    ```

## What the checks assert

`check.py` and `check.lua` walk the same surface side by side: styled names
(`add_entry` both sides, but `Notebook` vs `notebook`), the per-language
`to_text`/`as_string` split, `save_to` fed an `io.StringIO` in Python and a
closure in Lua, docstrings in `__doc__`, and both generated stubs containing the
styled declarations (the LuaCATS stub carries the doc text Lua drops at runtime).

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/07-multilang