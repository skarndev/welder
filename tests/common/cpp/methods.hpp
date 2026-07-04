#pragma once
// Constructors & methods — mirrors tests/test_methods.py. Also home to `Counter`,
// which the "methods resolve the same way" cases in test_resolution.py rely on.
//
// The cases live in namespace `methods`, bound under a `methods` submodule via
// WELDER_TEST_BE::bind_namespace so the Python package mirrors this file.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.

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

} // namespace methods

inline void register_methods(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "methods")};
    WELDER_TEST_BE::bind_namespace<^^methods>(sub);
}
