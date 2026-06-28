// Test extension exercising every member-resolution case. Built twice — once
// consuming `import welder;` and once consuming welder header-only — from this
// single source, selected by WELDER_TEST_HEADER_ONLY. The module name comes from
// WELDER_TEST_MODNAME so each build produces a distinct importable module.
#include <cstdint>
#include <string>

#ifdef WELDER_TEST_HEADER_ONLY
#  include <welder/welder.hpp>
#else
import welder;
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/python.hpp>

// ---- automatic policy (the default) ----------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Automatic {
    int kept = 0;                                                  // bound
    [[=welder::mark::exclude]]                    int excl_all = 0; // excluded (all)
    [[=welder::mark::exclude(welder::lang::py)]]  int excl_py = 0;  // excluded (py)
    [[=welder::mark::exclude(welder::lang::lua)]] int excl_lua = 0; // excluded lua only -> kept for py
    [[=welder::mark::include(welder::lang::py)]]  int incl_py = 0;  // redundant under automatic -> kept
};

// ---- opt_in policy ----------------------------------------------------------
struct [[=welder::weld(welder::lang::py)]] [[=welder::policy::opt_in]]
OptIn {
    int unmarked = 0;                                              // not opted in -> not bound
    [[=welder::mark::include]]                    int incl_all = 0; // included (all) -> bound
    [[=welder::mark::include(welder::lang::py)]]  int incl_py = 0;  // included (py) -> bound
    [[=welder::mark::include(welder::lang::lua)]] int incl_lua = 0; // included lua only -> not bound for py
    [[=welder::mark::include(welder::lang::py)]]
    [[=welder::mark::exclude(welder::lang::py)]]  int incl_then_excl = 0; // exclude wins -> not bound
};

// ---- read/write roundtrip ---------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Values {
    int i = 0;
    double d = 0.0;
    std::string s;
};

// ---- constructors + methods -------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Counter {
    int count = 0;

    Counter() = default;
    Counter(int start) : count(start) {}          // overloaded constructor

    void increment() { ++count; }                 // method (mutating)
    void add(int n) { count += n; }               // method with a parameter
    int value() const { return count; }           // const method
    static int version() { return 7; }            // static method

    [[=welder::mark::exclude]] void secret() {}    // excluded -> not bound
};

// ---- overloaded methods -----------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Calc {
    int base = 0;

    Calc() = default;
    Calc(int b) : base(b) {}

    int sum(int a) const { return base + a; }            // overload 1
    int sum(int a, int b) const { return base + a + b; } // overload 2
};

#ifndef WELDER_TEST_MODNAME
#  define WELDER_TEST_MODNAME welder_test_bindings
#endif

PYBIND11_MODULE(WELDER_TEST_MODNAME, m) {
    m.doc() = "welder test bindings";
    welder::py::bind<Automatic>(m);
    welder::py::bind<OptIn>(m);
    welder::py::bind<Values>(m);
    welder::py::bind<Counter>(m);
    welder::py::bind<Calc>(m);
}
