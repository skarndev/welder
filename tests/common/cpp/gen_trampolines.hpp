#pragma once
// Virtual types whose trampolines are GENERATED (welder::rods::trampolines), not
// hand-written — the counterpart to overridable.hpp, which hand-writes them. The
// generated header (produced at build time by a WELDER_TRAMPOLINES_MAIN generator)
// supplies the `PyT` subclasses and the trampoline_for<T> registrations; this file
// only declares the welded types and the register hook. It is bound under a
// `gen_trampolines` submodule, mirrored by tests/python/test_gen_trampolines.py.
//
// bindings.cpp #includes this, then the generated header, before calling the register
// hook — so the trampoline_for specializations are visible when weld_namespace runs.
//
// Deliberately exercises the interesting paths: a plain polymorphic base, a DERIVED
// welded type that overrides an inherited virtual (coverage must fold inherited
// virtuals in), an abstract base with a pure virtual, and a per-method bind_flat.

#include <string>

namespace gen_trampolines {

struct
[[=welder::weld(welder::lang::py)]]
Beast {
    virtual ~Beast() = default;

    [[=welder::doc("The sound this beast makes.")]]
    virtual std::string cry() const { return "..."; }

    virtual int limbs() const { return 4; }

    // Deliberately bound flat: stays callable, not routed through the trampoline, so
    // the generator must NOT emit an override for it.
    [[=welder::rods::python::bind_flat]]
    virtual std::string realm() const { return "Animalia"; }

    // A non-virtual method calling the virtuals polymorphically: observing it from
    // Python proves a C++ call dispatches into a Python override.
    std::string portrait() const {
        return cry() + " on " + std::to_string(limbs()) + " limbs";
    }
};

// A derived welded type that inherits cry()/limbs() and adds soar(). Its generated
// trampoline must cover the inherited virtuals too (via overridable_virtuals).
struct
[[=welder::weld(welder::lang::py)]]
Raptor : Beast {
    [[=welder::doc("How this raptor flies.")]]
    virtual std::string soar() const { return "glide"; }
};

// An abstract base with a pure virtual a Python subclass supplies.
struct
[[=welder::weld(welder::lang::py)]]
Figure {
    virtual ~Figure() = default;
    [[=welder::doc("The figure's area.")]]
    virtual double surface() const = 0;
    double scaled(double factor) const { return surface() * factor; }
};

} // namespace gen_trampolines

// The register hook needs the backend seam macros; the trampoline *generator* TU
// includes this header only for the type declarations (it defines no such macros), so
// guard the hook out there.
#ifdef WELDER_TEST_MODULE_T
inline void register_gen_trampolines(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "gen_trampolines")};
    WELDER_TEST_WELDER::weld_namespace<^^gen_trampolines>(sub);
}
#endif