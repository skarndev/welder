# 03 — Inheritance: native bases vs mixins

*Source: [`examples/cookbook/03-inheritance`][src].*

`weld` is a **discovery marker**, not an inheritance directive — a base does not
have to be welded for a derived type to bind. What the base's marker decides is
*how* it participates ([Inheritance](../guide/inheritance.md)):

| Base | Becomes |
|---|---|
| **welded** | a **native base class** — `issubclass` holds, members arrive via the MRO |
| **not welded** | a **mixin** — its eligible members are flattened into the derived binding |

## The three flavors

```cpp
// Welded base + welded derived -> native inheritance.
struct [[=welder::weld(welder::lang::py)]] Vehicle { ... };
struct [[=welder::weld(welder::lang::py)]] Car : public Vehicle { ... };

// NOT welded -> a mixin: never a Python class of its own; welded types deriving
// from it get its eligible members flattened in (its own marks still honored).
struct Serviceable {
    int last_service_year{2026};
    std::string service() { return "serviced"; }
    [[=welder::mark::exclude]] int service_secret{0};
};

// One native base + one flattened mixin.
struct [[=welder::weld(welder::lang::py)]]
Truck : public Vehicle, public Serviceable { ... };

// A welded ancestor reached only THROUGH a non-welded bridge: the bridge
// flattens, the native link to the ancestor survives.
struct [[=welder::weld(welder::lang::py)]] Chassis { ... };
struct Prototype : public Chassis { ... };            // not welded
struct [[=welder::weld(welder::lang::py)]] Racer : public Prototype { ... };
```

!!! note "Order matters once"

    pybind11 requires a native base registered before its derived types.
    `weld_namespace` visits declarations in order, so declaring bases first (as
    C++ already forces) is all it takes.

## What the check asserts

`issubclass(Car, Vehicle)` and `issubclass(Racer, Chassis)` hold; `Serviceable`
and `Prototype` never appear as classes; `Truck` carries the mixin's
`last_service_year` / `service()` but not its excluded member.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/03-inheritance