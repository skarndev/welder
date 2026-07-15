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
struct skip_private : welder::carriages::greedy_resolution<> {
    static consteval bool hidden(std::meta::info entity) {
        if (!std::meta::has_identifier(entity))
            return false;
        const std::string_view name{std::meta::identifier_of(entity)};
        return name.starts_with('_') || name == "detail" || name == "impl";
    }

    // namespace-scope classes / functions / variables (bound_into = the swept
    // namespace — forwarded; a rule that doesn't need the context ignores it)
    static consteval bool member_participates(std::meta::info mem, welder::lang L,
                                              welder::policy_kind pol,
                                              std::meta::info bound_into) {
        return !hidden(mem) &&
               welder::carriages::greedy_resolution<>::member_participates(
                   mem, L, pol, bound_into);
    }

    // nested namespaces: prune `detail` & friends wholesale — never recursed
    static consteval bool namespace_participates(std::meta::info ns, welder::lang L,
                                                 welder::policy_kind pol,
                                                 std::meta::info bound_into) {
        return !hidden(ns) &&
               welder::carriages::greedy_resolution<>::namespace_participates(
                   ns, L, pol, bound_into);
    }

    // class members — fields, methods, operators, constructors — resolve here,
    // PER OVERLOAD: the driver computes each name's overload group from this
    // predicate. A SIGNATURE-level rule prunes exactly one sibling: the modern
    // label(string) binds while the legacy label(const char*, int) does not.
    static consteval bool takes_c_string(std::meta::info fn) {
        for (auto p : std::meta::parameters_of(fn))
            if (std::meta::dealias(std::meta::type_of(p)) ==
                std::meta::dealias(^^const char*))
                return true;
        return false;
    }
    static consteval bool class_member_participates(std::meta::info mem, welder::lang L,
                                                    welder::policy_kind pol,
                                                    std::meta::info bound_into) {
        if (hidden(mem) || (std::meta::is_function(mem) && takes_c_string(mem)))
            return false;
        return welder::carriages::greedy_resolution<>::class_member_participates(
            mem, L, pol, bound_into);
    }

    // keep the bindability gate's registration oracle consistent (see below)
    static consteval bool counts_as_registered(std::meta::info type, welder::lang L) {
        return !hidden(type) &&
               welder::carriages::greedy_resolution<>::counts_as_registered(type, L);
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

**Per-overload, signature-level resolution.** `class_member_participates` is
consulted for every field, method, operator and constructor — **per overload** —
and the driver computes each name's overload group from its verdicts before
handing the group to the rod. That is what lets a signature rule ("skip the
C-string legacy API") prune exactly one overload of `label` while its modern
sibling binds, identically on every rod. (The same mechanism is what makes
[`mark::exclude` on an individual overload or constructor](../guide/annotations.md)
work under ordinary stitch welding — the marks *are* the stock resolution's
per-member rule.) Each per-member predicate also receives the **bound-into
entity** (`std::meta::info`, trailing) — for class members the welded type,
held fixed while a non-welded base's members are flattened in — so a rule can
scope itself to a flattening target, e.g. admit a mixin's
[protected members](../guide/annotations.md#policyweld_protected-expose-the-protected-surface)
only into one specific derived binding via the optional
`protected_participates(mem, L, bound_into)` hook.

**Override the oracle too.** The gate's registration oracle
(`counts_as_registered`) is part of the resolution: the greedy default vouches
for any registrable class type, but a type your skip rule hides is *never*
registered — mirroring the rule in the oracle keeps a public signature that
names a hidden type a **compile-time** error instead of a call-time
unregistered-type surprise.

## What the check asserts

The public surface (`Reading`, `take_reading`, `API_LEVEL`, the `units`
submodule) binds exactly as under plain tack welding; `_CalibrationTable`,
`_reset_driver`, `_debug_flag`, the in-class `_raw`, the legacy
`label(const char*, int)` overload, and the entire `detail` namespace do not
exist.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/09-custom-traversal