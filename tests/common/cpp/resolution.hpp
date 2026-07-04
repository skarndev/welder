#pragma once
// Resolution cases — mirrors tests/test_resolution.py (same sections, same order).
//
// Bind targets here: automatic policy, opt_in policy, the read/write roundtrip /
// exact-bound-set type (Values), and access control (Access). The "methods
// resolve the same way as data members" section of test_resolution.py exercises
// `Counter`, which lives in methods.hpp — nothing to declare for it here.
//
// #included by bindings.cpp *after* the welder vocabulary and the active Python
// backend are in scope; this header deliberately does not include them itself (in
// the module-form build the vocabulary arrives via `import welder;`).
#include <string>

// The cases live in namespace `resolution`, bound under a `resolution` submodule
// via WELDER_TEST_BE::bind_namespace so the Python package mirrors this file.
namespace resolution {

// --- automatic policy: bind everything unless excluded ----------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Automatic {
    // bound
    int kept{0};

    // excluded (all languages)
    [[=welder::mark::exclude]]
    int excl_all{0};

    // excluded (py)
    [[=welder::mark::exclude(welder::lang::py)]]
    int excl_py{0};

    // excluded lua only -> kept for py
    [[=welder::mark::exclude(welder::lang::lua)]]
    int excl_lua{0};

    // redundant under automatic -> kept
    [[=welder::mark::include(welder::lang::py)]]
    int incl_py{0};
};

// --- opt_in policy: bind only what is explicitly included -------------------

struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::opt_in
]]
OptIn {
    // not opted in -> not bound
    int unmarked{0};

    // included (all languages) -> bound
    [[=welder::mark::include]]
    int incl_all{0};

    // included (py) -> bound
    [[=welder::mark::include(welder::lang::py)]]
    int incl_py{0};

    // included lua only -> not bound for py
    [[=welder::mark::include(welder::lang::lua)]]
    int incl_lua{0};

    // exclude wins -> not bound
    [[
      =welder::mark::include(welder::lang::py),
      =welder::mark::exclude(welder::lang::py)
    ]]
    int incl_then_excl{0};
};

// --- read/write roundtrip (and the exact bound set) -------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Values {
    int i{0};
    double d{0.0};
    std::string s;
};

// --- methods resolve the same way as data members ---------------------------
// Exercised against `Counter` (defined in methods.hpp, bound by register_methods).

// --- access control: only public members are bound --------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Access {
    // public data -> bound
    int visible{0};

    // public method -> bound
    int read_hidden() const {
        return hidden;
    }

private:
    // private data -> not bound
    int hidden{9};

    // private method -> not bound
    void helper() {}

protected:
    // protected data -> not bound
    int guarded{0};
};

} // namespace resolution

inline void register_resolution(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "resolution")};
    WELDER_TEST_BE::bind_namespace<^^resolution>(sub);
}
