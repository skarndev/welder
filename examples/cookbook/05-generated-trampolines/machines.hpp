#pragma once
// Cookbook 05 — the welded virtual types. No trampolines here: they are GENERATED
// at build time by the welder::rods::trampolines rod (see gen.cpp + CMakeLists.txt).
// This header is included by both the generator TU (no Python backend at all) and
// the binding TU, so it carries only the types and the welder markers.
#include <string>

#include <welder/vocabulary.hpp>

namespace machines {

// A polymorphic base. Its trampoline (PyEngine, in the generated header) covers
// hum() and rpm(); label() is bind_flat, so the generator must NOT emit an
// override for it.
struct
[[=welder::weld(welder::lang::py), =welder::doc("Something that spins.")]]
Engine {
    virtual ~Engine() = default;

    [[=welder::doc("The noise at idle.")]]
    virtual std::string hum() const { return "brrr"; }

    virtual int rpm() const { return 800; }

    [[=welder::rods::python::bind_flat]]
    virtual std::string label() const { return "engine"; }

    // Non-virtual, dispatches into the virtuals — observable proof that a C++
    // call reaches a Python override through the generated trampoline.
    std::string report() const { return hum() + " @ " + std::to_string(rpm()); }
};

// A DERIVED welded type: inherits hum()/rpm() and adds boost(). Its generated
// trampoline must cover the inherited virtuals too — welder folds the whole base
// chain into the slot set, and the generator emits all three overrides.
struct
[[=welder::weld(welder::lang::py), =welder::doc("An engine with a turbo.")]]
Turbo : Engine {
    [[=welder::doc("Extra thrust.")]]
    virtual int boost() const { return 2; }
};

// An abstract base: the generated trampoline doubles as the construction type,
// so Python subclasses can instantiate and implement watts().
struct
[[=welder::weld(welder::lang::py), =welder::doc("A power source.")]]
Generator {
    virtual ~Generator() = default;

    [[=welder::doc("Output power.")]]
    virtual int watts() const = 0;

    int kilowatts() const { return watts() / 1000; }
};

} // namespace machines