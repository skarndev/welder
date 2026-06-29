#pragma once
// Constructors & methods — mirrors tests/test_methods.py. Also home to `Counter`,
// which the "methods resolve the same way" cases in test_resolution.py rely on.
//
// #included by bindings.cpp after the welder vocabulary + pybind11 backend.

// --- constructors + methods -------------------------------------------------

struct
[[=welder::weld(welder::lang::py)]]
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
[[=welder::weld(welder::lang::py)]]
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

inline void register_methods(pybind11::module_& m) {
    welder::pybind11::bind<Counter>(m);
    welder::pybind11::bind<Calc>(m);
}
