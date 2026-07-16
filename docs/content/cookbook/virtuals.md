# 04 — Virtual methods: overriding from Python

*Source: [`examples/cookbook/04-virtuals`][src].*

To let a Python subclass override a C++ virtual, the binding needs a
**trampoline** — a C++ subclass with one `override` per virtual that forwards
into Python. welder automates everything *around* the trampoline (registration,
static coverage checking, the dispatch plumbing), while the trampoline
declarations themselves stay two mechanical lines each. Recipe
[05](generated-trampolines.md) removes even those.

## Base + trampoline

```cpp
struct
[[=welder::weld(welder::lang::py), =welder::doc("A programmable robot.")]]
Robot {
    virtual ~Robot() = default;
    virtual std::string name() const { return "unit"; }
    virtual int speed() const { return 1; }

    // Bound FLAT: callable, but not overridable — no trampoline slot, no
    // C++->Python dispatch cost.
    [[=welder::rods::python::bind_flat]]
    virtual std::string vendor() const { return "ACME"; }

    // Non-virtual, calls the virtuals polymorphically: observing it from Python
    // proves C++ dispatches into the Python override.
    std::string status() const { return name() + " @ speed " + std::to_string(speed()); }
};

struct PyRobot : Robot {
    WELDER_PY_TRAMPOLINE(PyRobot, Robot);
    std::string name() const override { WELDER_PY_OVERRIDE(name); }
    int speed() const override { WELDER_PY_OVERRIDE(speed); }
};
```

The macros are backend-neutral — the same trampoline source compiles under the
pybind11 *or* nanobind rod; only the included
`<welder/rods/python/<backend>/trampoline.hpp>` differs.

## Two discovery forms

welder must learn which trampoline serves which type. Explicit registration works
anywhere (and disambiguates); the annotation form needs no registration but the
trampoline must live in the type's namespace:

```cpp
// explicit:
template <> constexpr std::meta::info
    welder::rods::python::trampoline_for<robots::Robot> = ^^robots::PyRobot;

// by annotation:
struct [[=welder::rods::python::trampoline]] PyMission : Mission { ... };
```

A welded type with virtuals and *no* trampoline is a **compile error** (mark the
type or method `bind_flat` to opt out of overridability instead).

## Abstract bases

`Mission::duty()` is pure virtual. welder registers the trampoline as the
construction type, so Python subclasses instantiate and implement it; calling an
unoverridden pure virtual raises `RuntimeError` at call time (framework
behavior).

## What the check asserts

A Python `Scout(Robot)` changes what the *C++* `status()` returns — real
C++ → Python dispatch; `bind_flat` methods stay callable but a Python override of
them is invisible to C++; the abstract `Mission` is implementable from Python.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/04-virtuals