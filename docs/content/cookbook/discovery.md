# 02 — Discovery rules

*Source: [`examples/cookbook/02-discovery`][src].*

One `WELDER_MODULE` line binds a whole namespace; everything else in this recipe
is steered from the declarations. It is the [annotation
vocabulary](../guide/annotations.md) exercised end to end: policies, marks,
nested-namespace recursion, and `weld_as` renames.

## The moves, one by one

**The whole module in one line** — the namespace token doubles as the module
name, its `doc` becomes the module docstring, and the optional trailing block is
hand-written post-glue ([Namespaces & modules](../guide/namespaces-modules.md)):

```cpp
namespace [[=welder::doc("A small warehouse inventory.")]] inventory { ... }

WELDER_MODULE(inventory, pybind11) {
    module.attr("SCHEMA_VERSION") = 2;
}
```

**`automatic` vs `opt_in`** — the default policy binds every public member unless
excluded; `opt_in` binds only what is explicitly included:

```cpp
struct [[=welder::weld(welder::lang::py)]]
Item {
    std::string name;                                      // bound
    [[=welder::mark::exclude]] std::uint64_t cache_key{0}; // bound nowhere
};

struct [[=welder::weld(welder::lang::py), =welder::policy::opt_in]]
AuditRecord {
    int internal_id{0};                          // not included -> not bound
    [[=welder::mark::include]] std::string note; // bound
};
```

**Renaming with `weld_as`** — the bound name is forced *verbatim* (never through a
[name style](../guide/naming.md)); the C++ spelling is not exposed:

```cpp
struct [[=welder::weld(welder::lang::py), =welder::weld_as("Crate")]]
BigWoodenBox { ... };
```

**Nested namespaces** — a nested namespace with bound content becomes a
submodule; `mark::exclude` on a namespace prunes it wholesale, even when its
contents carry `weld` markers (the usual way to keep `detail`/`impl` out):

```cpp
namespace pricing { ... }                         // -> inventory.pricing
namespace [[=welder::mark::exclude]] detail { ... } // pruned wholesale
```

**Overloads** — a welded overload set binds under one name with call-time
dispatch; `restock(item, 5)` and `restock(item)` both work.

## What the check asserts

Everything above, from the Python side: `Crate` exists and `BigWoodenBox` does
not; the unwelded `Ledger` is invisible; `AuditRecord` exposes only the included
fields; `inventory.pricing.discounted(...)` is a submodule function;
`inventory.detail` does not exist.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/02-discovery