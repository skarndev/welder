# 09 — Custom traversal: a pruning tack weld

*Source: [`examples/cookbook/09-custom-traversal`][src].*

[Tack welding](tack-welding.md) binds an unannotated library *greedily* — which
also means it descends into the library's `detail` namespace and drags its
underscore-prefixed internals along. You can't `mark::exclude` a header you don't
own; what you *can* do is change **which entities participate**: that decision is
a **resolution** — a stateless struct of `consteval` predicates the traversal
carriage consults — and the shipped resolutions are ordinary structs, so
customizing one is plain inheritance
([Extending welder](../guide/extending.md#custom-traversal-resolutions-and-carriages)).

## The resolution

Greedy, minus the library's own privacy convention:

```cpp
struct skip_private : welder::carriages::greedy_resolution {
    static consteval bool hidden(std::meta::info entity) {
        if (!std::meta::has_identifier(entity))
            return false;
        const std::string_view name{std::meta::identifier_of(entity)};
        return name.starts_with('_') || name == "detail" || name == "impl";
    }

    // namespace-scope classes / functions / variables
    static consteval bool member_participates(std::meta::info mem, welder::lang L,
                                              welder::policy_kind pol) {
        return !hidden(mem) &&
               welder::carriages::greedy_resolution::member_participates(mem, L, pol);
    }

    // nested namespaces: prune `detail` & friends wholesale — never recursed
    static consteval bool namespace_participates(std::meta::info ns, welder::lang L,
                                                 welder::policy_kind pol) {
        return !hidden(ns) &&
               welder::carriages::greedy_resolution::namespace_participates(ns, L, pol);
    }

    // keep the bindability gate's registration oracle consistent (see below)
    static consteval bool counts_as_registered(std::meta::info type, welder::lang L) {
        return !hidden(type) &&
               welder::carriages::greedy_resolution::counts_as_registered(type, L);
    }
};
```

## Plugging it in

A carriage is `basic_carriage<Resolution>`, injected as `welder::welder`'s third
template argument — the same seam the stock stitch/tack carriages use:

```cpp
PYBIND11_MODULE(sensors, m) {
    using pruned_tack =
        welder::welder<welder::rods::pybind11::rod<>, welder::naming::none,
                       welder::carriages::basic_carriage<skip_private>>;
    pruned_tack::weld_namespace<^^sensorlib>(m);
}
```

## Two things worth noticing

**Override the oracle too.** The gate's registration oracle
(`counts_as_registered`) is part of the resolution: the greedy default vouches
for any registrable class type, but a type your skip rule hides is *never*
registered — mirroring the rule in the oracle keeps a public signature that
names a hidden type a **compile-time** error instead of a call-time
unregistered-type surprise.

**The seam is namespace-level.** A resolution decides which namespace members
participate, which nested namespaces recurse, and which bases are native
(`participates` / `is_native_base` / `native_bases` complete the contract) — it
is not consulted per *class member*. Inside a type you own, the
[marks](../guide/annotations.md) are the per-member tool; inside a type you
don't, exclude the whole type or wrap it.

## What the check asserts

The public surface (`Reading`, `take_reading`, `API_LEVEL`, the `units`
submodule) binds exactly as under plain tack welding; `_CalibrationTable`,
`_reset_driver`, `_debug_flag` and the entire `detail` namespace do not exist.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/09-custom-traversal