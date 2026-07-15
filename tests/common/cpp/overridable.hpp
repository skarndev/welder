#pragma once
// Virtual-method overriding via a welder trampoline — mirrors tests/test_trampoline.py.
//
// The virtual class is backend-neutral; the trampoline uses welder's neutral
// WELDER_PY_TRAMPOLINE / WELDER_PY_OVERRIDE macros, so the SAME source binds under
// either Python rod. The binding TU must include the active backend's
// <welder/rods/python/<backend>/trampoline.hpp> *before* this header (for the macros
// and welder::rods::python::{trampoline_for,bind_flat}). The Lua backends do not
// include this header — trampolines are a Python-family concept.
//
// The cases live in namespace `overridable`, bound under an `overridable` submodule
// via WELDER_TEST_WELDER::weld_namespace so the Python package mirrors this file.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.

namespace overridable {

// A polymorphic base a Python subclass can override.
struct
[[=welder::weld(welder::lang::py)]]
Animal {
    virtual ~Animal() = default;

    [[=welder::doc("The sound this animal makes.")]]
    virtual std::string speak() const { return "..."; }

    virtual int legs() const { return 4; }

    // A virtual deliberately bound *flat*: it stays a plain, callable method but is
    // not routed through the trampoline, so it needs no override and drops out of the
    // slot count and coverage check. C++ never dispatches it back into Python.
    [[=welder::rods::python::bind_flat]]
    virtual std::string kingdom() const { return "Animalia"; }

    // A non-virtual method that calls the virtuals polymorphically: observing its
    // result from Python proves a C++ call dispatches into the Python override.
    std::string describe() const {
        return speak() + " on " + std::to_string(legs()) + " legs";
    }
};

// The trampoline: one neutral storage line + one neutral line per overridable
// virtual. `kingdom()` is bind_flat, so it is intentionally NOT overridden here.
struct PyAnimal : Animal {
    WELDER_PY_TRAMPOLINE(Animal);
    std::string speak() const override { WELDER_PY_OVERRIDE(speak); }
    int legs() const override { WELDER_PY_OVERRIDE(legs); }
};

// An *abstract* base with a pure virtual a Python subclass supplies. Because welder
// registers the concrete trampoline's default constructor (see construction_type),
// the base is constructible from Python and a subclass works; an unoverridden pure
// virtual raises at call time (nanobind/pybind11 behavior), not at construction.
struct
[[=welder::weld(welder::lang::py)]]
Shape {
    virtual ~Shape() = default;

    [[=welder::doc("The shape's area.")]]
    virtual double area() const = 0; // pure virtual — no C++ fallback

    // A concrete C++ method calling the pure virtual polymorphically.
    double scaled_area(double factor) const { return area() * factor; }
};

// This trampoline uses the *annotation* form: the [[=trampoline]] mark lets welder
// discover it by scanning Shape's namespace — no trampoline_for<Shape> needed.
struct [[=welder::rods::python::trampoline]] PyShape : Shape {
    WELDER_PY_TRAMPOLINE(Shape);
    double area() const override { WELDER_PY_OVERRIDE(area); }
};

// A *derived* welded polymorphic type. Bird adds one new virtual (fly) but inherits
// Animal's speak()/legs() without re-declaring them. Its trampoline must therefore
// cover the INHERITED virtuals too — a Python subclass of Bird can override speak(),
// and that dispatch runs through Bird's own trampoline, not Animal's. welder's
// virtual_slot_count / trampoline_covers fold inherited virtuals in (they walk the
// base chain, not just members_of), so PyBird must redeclare speak + legs + fly;
// omitting the inherited ones is a coverage static_assert failure.
struct
[[=welder::weld(welder::lang::py)]]
Bird : Animal {
    [[=welder::doc("How this bird flies.")]]
    virtual std::string fly() const { return "flap"; }
};

struct PyBird : Bird {
    WELDER_PY_TRAMPOLINE(Bird);
    std::string speak() const override { WELDER_PY_OVERRIDE(speak); }  // inherited
    int legs() const override { WELDER_PY_OVERRIDE(legs); }            // inherited
    std::string fly() const override { WELDER_PY_OVERRIDE(fly); }      // own
};

// The signature shapes the cases above do not exercise: virtuals WITH parameters
// (the argument-forwarding path), a non-const noexcept virtual, an OVERLOADED
// virtual pair, and a protected NVI hook. Each has a C++ caller so the tests can
// observe the C++ -> Python dispatch, not just the Python-level attribute.
struct
[[=welder::weld(welder::lang::py)]]
Robot {
    virtual ~Robot() = default;

    // A parameterful virtual: the override must receive both arguments intact.
    virtual std::string obey(const std::string& order, int times) const {
        std::string out{};
        for (int i{0}; i < times; ++i)
            out += order;
        return out;
    }

    // Non-const + noexcept: the qualifier combinations beyond `const`.
    virtual int recharge(int amount) noexcept { return charge += amount; }

    // An overloaded virtual: `^^Robot::send` would name an overload set (ill-formed),
    // so the trampoline must use WELDER_PY_OVERRIDE_AS + virtual_slot per overload.
    virtual std::string send(int code) const { return "int:" + std::to_string(code); }
    virtual std::string send(const std::string& text) const { return "str:" + text; }

    // C++ callers dispatching the virtuals polymorphically.
    std::string march(int steps) const { return obey("step ", steps); }
    std::string transmit() const { return send(7) + "|" + send(std::string{"hi"}); }
    std::string handshake() const { return "proto=" + protocol(); }

    int charge{0};

  protected:
    // An NVI hook: protected, so it is never *bound* — but it IS an overridable
    // trampoline slot (a Python subclass overrides it by plain attribute lookup,
    // which needs no binding), reached from C++ via handshake().
    virtual std::string protocol() const { return "asimov"; }
};

struct PyRobot : Robot {
    WELDER_PY_TRAMPOLINE(Robot);
    std::string obey(const std::string& order, int times) const override {
        WELDER_PY_OVERRIDE(obey, order, times);
    }
    int recharge(int amount) noexcept override { WELDER_PY_OVERRIDE(recharge, amount); }
    // Each overload dispatches on its own slot reflection; the textual `send` in the
    // macro spells only the qualified base fallback, where overload resolution picks
    // the right overload from the forwarded argument.
    std::string send(int code) const override {
        WELDER_PY_OVERRIDE_AS((welder::rods::python::virtual_slot(
                                  ^^Robot, "send", ^^std::string(int) const)),
                              send, code);
    }
    std::string send(const std::string& text) const override {
        WELDER_PY_OVERRIDE_AS(
            (welder::rods::python::virtual_slot(
                ^^Robot, "send", ^^std::string(const std::string&) const)),
            send, text);
    }
    std::string protocol() const override { WELDER_PY_OVERRIDE(protocol); }
};

// A COVARIANT override: Tree::parent narrows Plant::parent's return type. That is
// the same vtable slot, so Tree has exactly ONE overridable slot, kept with the
// most-derived (Tree*) signature — its trampoline redeclares that narrowed form.
// The returned pointer is non-owning, hence the reference return policy.
struct
[[=welder::weld(welder::lang::py)]]
Plant {
    virtual ~Plant() = default;

    [[=welder::return_policy(welder::rv::reference)]]
    virtual Plant* parent() const { return nullptr; }

    // A C++ caller: a Python override returning an instance (or None) is observed
    // from C++ as a Plant* (or nullptr).
    bool orphan() const { return parent() == nullptr; }
};

struct
[[=welder::weld(welder::lang::py)]]
Tree : Plant {
    [[=welder::return_policy(welder::rv::reference)]]
    Tree* parent() const override { return nullptr; }
};

struct PyPlant : Plant {
    WELDER_PY_TRAMPOLINE(Plant);
    Plant* parent() const override { WELDER_PY_OVERRIDE(parent); }
};

struct PyTree : Tree {
    WELDER_PY_TRAMPOLINE(Tree);
    Tree* parent() const override { WELDER_PY_OVERRIDE(parent); }  // the narrowed slot
};

} // namespace overridable

// Animal uses the explicit registration form (trampoline_for); Shape uses the
// annotation form above. Both discovery paths are thus exercised.
template <> constexpr std::meta::info
    welder::rods::python::trampoline_for<overridable::Animal> = ^^overridable::PyAnimal;
template <> constexpr std::meta::info
    welder::rods::python::trampoline_for<overridable::Bird> = ^^overridable::PyBird;
template <> constexpr std::meta::info
    welder::rods::python::trampoline_for<overridable::Robot> = ^^overridable::PyRobot;
template <> constexpr std::meta::info
    welder::rods::python::trampoline_for<overridable::Plant> = ^^overridable::PyPlant;
template <> constexpr std::meta::info
    welder::rods::python::trampoline_for<overridable::Tree> = ^^overridable::PyTree;

inline void register_overridable(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "overridable")};
    WELDER_TEST_WELDER::weld_namespace<^^overridable>(sub);
}