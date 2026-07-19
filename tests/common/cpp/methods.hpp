#pragma once
// Constructors & methods — mirrors tests/test_methods.py. Also home to `Counter`,
// which the "methods resolve the same way" cases in test_resolution.py rely on.
//
// The cases live in namespace `methods`, bound under a `methods` submodule via
// WELDER_TEST_WELDER::weld_namespace so the Python package mirrors this file.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.
#include <string>

namespace methods {

// --- constructors + methods -------------------------------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Counter {
    int count{0};

    Counter() = default;

    // overloaded constructor
    Counter(int start) : count{start} {}

    // method (mutating)
    void increment() {
        ++count;
    }

    // method with a parameter
    void add(int n) {
        count += n;
    }

    // const method
    int value() const {
        return count;
    }

    // static method
    static int version() {
        return 7;
    }

    // excluded -> not bound
    [[=welder::mark::exclude]]
    void secret() {}
};

// --- overloaded methods -----------------------------------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Calc {
    int base{0};

    Calc() = default;
    Calc(int b) : base{b} {}

    // overload 1
    int sum(int a) const {
        return base + a;
    }

    // overload 2
    int sum(int a, int b) const {
        return base + a + b;
    }
};

// --- aggregate initialization -----------------------------------------------
// A simple aggregate (no user-declared constructors): it cannot be built as
// Vec2(x, y) in C++, only brace-initialized Vec2{x, y}. welder synthesizes a
// field constructor so Python still gets Vec2(x, y) (and Vec2(x=..., y=...)),
// alongside the default Vec2().

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Vec2 {
    double x{0.0};
    double y{0.0};
};

// --- NSDMI defaults on the synthesized field constructor ---------------------
// The fields after the last one WITHOUT a default member initializer are the
// omissible suffix: aggregate init fills omitted trailing elements from their
// NSDMIs, so Python attaches them as real keyword defaults and the Lua rods
// expose one constructor arity per omissible tail. `samples` has an NSDMI but
// sits BEFORE the required `title`, so it stays required — a parameter list
// allows no gaps, exactly like C++ default function arguments.

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Window {
    int samples{4};
    std::string title;
    int width{800};
    int height{600};
    bool resizable{true};
};

// Const members keep a struct an aggregate: an immutable settings-style value
// binds with read-only fields while the synthesized field constructor (and its
// NSDMI defaults) still brace-initializes it.

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Frozen {
    const std::string name;
    const int level{1};
};

} // namespace methods

inline void register_methods(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "methods")};
    WELDER_TEST_WELDER::weld_namespace<^^methods>(sub);
}
