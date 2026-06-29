#pragma once
// Resolution cases — mirrors tests/test_resolution.py (same sections, same order).
//
// Bind targets here: automatic policy, opt_in policy, the read/write roundtrip /
// exact-bound-set type (Values), and access control (Access). The "methods
// resolve the same way as data members" section of test_resolution.py exercises
// `Counter`, which lives in methods.hpp — nothing to declare for it here.
//
// #included by bindings.cpp *after* the welder vocabulary and the pybind11
// backend are in scope; this header deliberately does not include them itself (in
// the module-form build the vocabulary arrives via `import welder;`).
#include <string>

// --- automatic policy: bind everything unless excluded ----------------------
struct [[=welder::weld(welder::lang::py)]]
Automatic {
    int kept{0};                                                   // bound
    [[=welder::mark::exclude]]                    int excl_all{0};  // excluded (all)
    [[=welder::mark::exclude(welder::lang::py)]]  int excl_py{0};   // excluded (py)
    [[=welder::mark::exclude(welder::lang::lua)]] int excl_lua{0};  // excluded lua only -> kept for py
    [[=welder::mark::include(welder::lang::py)]]  int incl_py{0};   // redundant under automatic -> kept
};

// --- opt_in policy: bind only what is explicitly included -------------------
struct [[=welder::weld(welder::lang::py)]] [[=welder::policy::opt_in]]
OptIn {
    int unmarked{0};                                               // not opted in -> not bound
    [[=welder::mark::include]]                    int incl_all{0};  // included (all) -> bound
    [[=welder::mark::include(welder::lang::py)]]  int incl_py{0};   // included (py) -> bound
    [[=welder::mark::include(welder::lang::lua)]] int incl_lua{0};  // included lua only -> not bound for py
    [[=welder::mark::include(welder::lang::py)]]
    [[=welder::mark::exclude(welder::lang::py)]]  int incl_then_excl{0}; // exclude wins -> not bound
};

// --- read/write roundtrip (and the exact bound set) -------------------------
struct [[=welder::weld(welder::lang::py)]]
Values {
    int i{0};
    double d{0.0};
    std::string s;
};

// --- methods resolve the same way as data members ---------------------------
// Exercised against `Counter` (defined in methods.hpp, bound by register_methods).

// --- access control: only public members are bound --------------------------
struct [[=welder::weld(welder::lang::py)]]
Access {
    int visible{0};                            // public data   -> bound
    int read_hidden() const { return hidden; } // public method -> bound

private:
    int hidden{9};               // private data  -> not bound
    void helper() {}             // private method-> not bound

protected:
    int guarded{0};              // protected data-> not bound
};

inline void register_resolution(pybind11::module_& m) {
    welder::pybind11::bind<Automatic>(m);
    welder::pybind11::bind<OptIn>(m);
    welder::pybind11::bind<Values>(m);
    welder::pybind11::bind<Access>(m);
}
