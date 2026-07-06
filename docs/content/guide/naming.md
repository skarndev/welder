# Naming conventions

Your C++ follows one house style; the language you bind to usually wants another.
Idiomatic C++ has `processFile`, `HTTPServer`, `maxRetries`; [PEP 8][pep8] asks a
Python caller for `process_file`, `HttpServer`, `max_retries`. welder bridges the
two with a pluggable **name style** — a policy handed to `welder::welder` that every
generated name flows through — plus a per-entity **`weld_as`** override for the cases
a rule can't capture.

By default nothing is renamed: `welder::welder<Rod>` binds each C++ identifier
verbatim (the style is [`welder::naming::none`](#name-styles)).

[pep8]: https://peps.python.org/pep-0008/

## A style is the second `welder::welder` argument

```cpp
#include <welder/rods/python/pybind11/rod.hpp>
#include <welder/rods/python/naming.hpp>   // welder::rods::python::pep8

using weld = welder::welder<welder::rods::pybind11::rod,
                            welder::rods::python::pep8>;

PYBIND11_MODULE(mymod, m) {
    weld::weld_module<^^mymod>(m);
}
```

`pep8` is the ready-made Python style: **CapWords** (PascalCase) for classes and
enum types, **snake_case** for methods, functions, fields and module variables, and
enum members left as authored. So:

```python
>>> mymod.GeometryHelper            # class: CapWords (unchanged here)
>>> h.process_file(...)             # processFile  -> process_file
>>> mymod.max_retries               # maxRetries   -> max_retries
```

The same style plugs into the nanobind rod, the sol2 rod, and the LuaCATS stub
generator — it is rod-agnostic. (For Lua you would typically pick a Lua-flavoured
style, or `none`; `pep8` lives with the Python rods because that is its home.)

## The style knows what it is naming

A style is asked to name each entity through a **per-kind hook**, because the same
identifier is styled differently by kind — PEP 8 PascalCases a class but snake_cases
a method:

```cpp
namespace welder::naming {
    struct my_style : /* a base */ {
        static consteval std::string transform_class(std::meta::info);
        static consteval std::string transform_enum(std::meta::info);
        static consteval std::string transform_enumerator(std::meta::info);
        static consteval std::string transform_method(std::meta::info);
        static consteval std::string transform_static_method(std::meta::info);
        static consteval std::string transform_function(std::meta::info);
        static consteval std::string transform_field(std::meta::info);
        static consteval std::string transform_variable(std::meta::info);
        static consteval std::string transform_submodule(std::meta::info);
    };
}
```

You never inspect the reflection to discover the kind; the driver already knows what
it is binding and calls the matching hook. In practice you **inherit** a base and
override only the hooks that differ. That is exactly how `pep8` is built — inherit
the all-snake_case style, override classes/enums to CapWords, keep enum members
verbatim:

```cpp
struct pep8 : welder::naming::snake_case {
    static consteval std::string transform_class(std::meta::info e) {
        return welder::naming::restyle(std::meta::identifier_of(e),
                                       welder::naming::case_kind::pascal);
    }
    static consteval std::string transform_enum(std::meta::info e) {
        return welder::naming::restyle(std::meta::identifier_of(e),
                                       welder::naming::case_kind::pascal);
    }
    static consteval std::string transform_enumerator(std::meta::info e) {
        return std::string{std::meta::identifier_of(e)};   // as authored
    }
};
```

### The word round-trip (source spelling doesn't matter)

The hard part is that the input format is unknown — an identifier might arrive as
`snake_case`, `camelCase`, `PascalCase` or `SCREAMING_CASE`. The core
(`<welder/naming.hpp>`) solves it by first **splitting** an identifier into its words
and then **re-joining** them in the target convention, so the result is stable
whatever the source:

```cpp
welder::naming::split_words("HTTPServer")   // -> {"http", "server"}   (acronym run)
welder::naming::split_words("processFile")  // -> {"process", "file"}  (camel hump)
welder::naming::restyle("process_file", welder::naming::case_kind::pascal)  // "ProcessFile"
welder::naming::restyle("maxRetries",   welder::naming::case_kind::snake)   // "max_retries"
```

Splitting breaks on underscores/hyphens, camel-case humps and acronym boundaries;
leading/trailing fixup underscores (`_private`, `type_`) survive. Every hook that
just applies one convention can be written with `restyle`.

## Name styles

| Style | Effect |
|---|---|
| `welder::naming::none` | Identity — bind the C++ identifier unchanged (the default). |
| `welder::naming::snake_case` | `foo_bar` everywhere. |
| `welder::naming::pascal_case` | `FooBar` everywhere. |
| `welder::naming::camel_case` | `fooBar` everywhere. |
| `welder::naming::screaming_snake_case` | `FOO_BAR` everywhere. |
| `welder::naming::kebab_case` | `foo-bar` everywhere. |
| `welder::rods::python::pep8` | PEP 8 mix (CapWords types, snake_case rest). |

The single-convention styles apply the same convention to *every* kind; use one
directly, or inherit it and override the exceptions (as `pep8` does).

## `weld_as`: force a name verbatim

Some names a rule can't derive — a C++ method whose Python name should be something
unrelated, or a per-language spelling. `weld_as` is the ultimate override: the string
is used **verbatim** and never flows through the style.

```cpp
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]]
       [[=welder::policy::opt_in]]
FileProcessor {
    [[=welder::mark::include]]
    [[=welder::weld_as(welder::lang::py,  "process")]]   // -> process()  in Python
    [[=welder::weld_as(welder::lang::lua, "Process")]]   // -> Process()  in Lua
    void processImpl();

    [[=welder::mark::include, =welder::weld_as("VERSION")]]  // every language, verbatim
    static const char* version_string();

    [[=welder::mark::include]]
    [[=welder::weld_as(welder::lang::py, welder::lang::lua, "flush")]]  // both, one name
    void flushEverything();
};
```

The name is always the **last** argument; any languages it applies to come first.
`weld_as("name")` (no marker) covers every language; `weld_as(lang, "name")` scopes it
to one; `weld_as(lang, lang, …, "name")` names several at once. Repeat the annotation
to give a *different* name per language (as `processImpl` does above). Being verbatim,
it bypasses the name style entirely — welder does not touch the string.

A style or `weld_as` that renames a **type** is honoured everywhere it appears —
its declaration, and every reference to it: field/parameter/return types, base-class
lists, and inside container types (`Rect[]`, `table<string, Rect>`). This holds for
the runtime bindings and for the generated LuaCATS (`---@meta`) stub alike; in the
stub, references are emitted with the raw C++ name and reconciled with their
declarations in a final pass, so declaration order never matters.
