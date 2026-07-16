#pragma once
// Copy-constructor cases — mirrors tests/test_copying.py (same sections, same
// order).
//
// The copy constructor never binds as an init overload; it is the rod's to
// spell. The Python rods bind it as the copy protocol (__copy__/__deepcopy__,
// both delegating to the C++ copy constructor); the Lua rods ignore it (Lua
// has no copy protocol). Admission mirrors the default constructor's: an
// implicit copy constructor is admitted whenever the type is
// copy-constructible; a declared one's explicit marks are honored (per
// language when scoped), but opt_in's default-out is not. Move constructors
// never bind at all — an include/only mark on one is a hard error (locked by
// negcompile.move_ctor_marked).
//
// #included by bindings.cpp *after* the welder vocabulary, the active Python
// backend AND its <welder/rods/python/<backend>/trampoline.hpp> (for the
// polymorphic cases' WELDER_PY_* macros) are in scope; this header deliberately
// does not include them itself.
#include <string>

// The cases live in namespace `copying`, bound under a `copying` submodule via
// WELDER_TEST_WELDER::weld_namespace so the Python package mirrors this file.
namespace copying {

// --- implicit copy constructor: the copy protocol rides along ----------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Sheet {
    int width{0};
    std::string title;
};

// --- deleted copy: not copy-constructible -> no copy protocol ----------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Pinned {
    Pinned() = default;
    Pinned(const Pinned&) = delete;
    Pinned& operator=(const Pinned&) = delete;

    int tag{7};
};

// --- a declared copy constructor's marks are honored: excluded ---------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Sealed {
    Sealed() = default;

    [[=welder::mark::exclude]]
    Sealed(const Sealed&) = default;

    int n{1};
};

// --- ... and per language: excluded for py only ------------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
PyBlocked {
    PyBlocked() = default;

    [[=welder::mark::exclude(welder::lang::py)]]
    PyBlocked(const PyBlocked&) = default;

    int n{2};
};

// --- opt_in: a declared, unmarked copy constructor stays admitted ------------
// The mirror of the default constructor's opt_in exemption: you cannot
// `include` an implicit copy constructor, so filtering the declared spelling
// by default would make `T(const T&) = default;` (a C++ no-op) silently change
// the binding.

struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::opt_in
]]
Choosy {
    Choosy() = default;
    Choosy(const Choosy&) = default;

    [[=welder::mark::include]]
    int chosen{3};
};

// --- a declared (unmarked) move constructor is skipped structurally ----------
// No error, nothing bound for it; the copy protocol is untouched.

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Shifty {
    Shifty() = default;
    Shifty(const Shifty&) = default;
    Shifty(Shifty&&) noexcept = default;

    int n{4};
};

// --- a polymorphic type: the copy protocol stays subclass-faithful -----------
// The base binding carries __copy__/__deepcopy__ (Brush is concrete and
// copy-constructible). Copying an instance of a *Python subclass* preserves
// its dynamic type, its __dict__ and its virtual dispatch: the protocol
// re-runs the registered copy __init__ on a type(self).__new__ shell, and the
// trampoline's copy-from-base constructor (declared by WELDER_PY_TRAMPOLINE)
// lets the backend construct the ALIAS payload there — without it the copy
// would hold a plain Brush and stop dispatching into Python. Python-only
// (trampolines are a Python-family concept, like overridable.hpp).

struct
[[=welder::weld(welder::lang::py)]]
Brush {
    virtual ~Brush() = default;
    Brush() = default;
    Brush(const Brush&) = default;

    virtual std::string stroke() const {
        return "solid";
    }

    // A C++ caller dispatching the virtual polymorphically: observing its result
    // on a copy proves the copy still dispatches into the Python override.
    std::string paint() const {
        return "paint:" + stroke();
    }

    int width{1};
};

struct [[=welder::rods::python::trampoline]] PyBrush : Brush {
    WELDER_PY_TRAMPOLINE(PyBrush, Brush);
    std::string stroke() const override { WELDER_PY_OVERRIDE(stroke); }
};

// --- an abstract type is not copy-constructible -> no copy protocol ----------

struct
[[=welder::weld(welder::lang::py)]]
Stencil {
    virtual ~Stencil() = default;

    virtual int holes() const = 0; // pure -> Stencil itself never copies
};

struct [[=welder::rods::python::trampoline]] PyStencil : Stencil {
    WELDER_PY_TRAMPOLINE(PyStencil, Stencil);
    int holes() const override { WELDER_PY_OVERRIDE(holes); }
};

} // namespace copying

inline void register_copying(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "copying")};
    WELDER_TEST_WELDER::weld_namespace<^^copying>(sub);
}