#pragma once
// Constructors & methods — mirrors tests/test_methods.py. Also home to `Counter`,
// which the "methods resolve the same way" cases in test_resolution.py rely on.
//
// #included by bindings.cpp after the welder vocabulary + pybind11 backend.

// --- constructors + methods -------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Counter {
    int count{0};

    Counter() = default;
    Counter(int start) : count{start} {}          // overloaded constructor

    void increment() { ++count; }                 // method (mutating)
    void add(int n) { count += n; }               // method with a parameter
    int value() const { return count; }           // const method
    static int version() { return 7; }            // static method

    [[=welder::mark::exclude]] void secret() {}    // excluded -> not bound
};

// --- overloaded methods -----------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Calc {
    int base{0};

    Calc() = default;
    Calc(int b) : base{b} {}

    int sum(int a) const { return base + a; }            // overload 1
    int sum(int a, int b) const { return base + a + b; } // overload 2
};

inline void register_methods(pybind11::module_& m) {
    welder::pybind11::bind<Counter>(m);
    welder::pybind11::bind<Calc>(m);
}
