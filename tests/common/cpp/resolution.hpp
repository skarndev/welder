#pragma once
// Resolution cases — mirrors tests/test_resolution.py (same sections, same order).
//
// Bind targets here: automatic policy, opt_in policy, the read/write roundtrip /
// exact-bound-set type (Values), and access control (Access). The "methods
// resolve the same way as data members" section of test_resolution.py exercises
// `Counter`, which lives in methods.hpp — nothing to declare for it here.
//
// #included by bindings.cpp *after* the welder vocabulary and the active Python
// backend are in scope; this header deliberately does not include them itself.
#include <string>

// The cases live in namespace `resolution`, bound under a `resolution` submodule
// via WELDER_TEST_WELDER::weld_namespace so the Python package mirrors this file.
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

    // only(py): the closed-world counterpart of exclude -> py, not lua (nor any
    // language welded later)
    [[=welder::mark::only(welder::lang::py)]]
    int only_py{0};

    // exclude wins over only -> bound nowhere
    [[
      =welder::mark::only(welder::lang::py),
      =welder::mark::exclude(welder::lang::py)
    ]]
    int only_then_excl{0};
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

    // only(py) is also the opt-in -> bound for py without an include, not lua
    [[=welder::mark::only(welder::lang::py)]]
    int only_py{0};

    // only(lua) likewise -> lua, not py
    [[=welder::mark::only(welder::lang::lua)]]
    int only_lua{0};
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
    // protected data -> not bound (no policy::weld_protected on this type)
    int guarded{0};
};

// --- policy::weld_protected: admit protected members -------------------------
// Combinable with automatic/opt_in (a separate annotation, not a policy kind):
// it makes protected members VISIBLE to the resolution; policy + marks then
// resolve them exactly like public ones. Private members stay out regardless.

struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::weld_protected
]]
Shielded {
    // the public surface, calling through the protected one
    int total() const {
        return base() + boost;
    }

protected:
    // protected method -> bound
    int base() const {
        return 40;
    }

    // protected overloads -> one bound group
    int scale(int f) const {
        return boost * f;
    }
    double scale(double f) const {
        return boost * f;
    }

    // protected static method -> bound
    static int origin() {
        return 7;
    }

    // protected data -> bound, read/write
    int boost{2};

    // marks still resolve on protected members: excluded -> not bound
    [[=welder::mark::exclude]]
    int tuning{0};

private:
    // private stays out — weld_protected never reaches it
    int core{99};

    void internal() {}
};

// language-scoped: protected members bind for py only; lua sees the public
// surface alone
struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::weld_protected(welder::lang::py)
]]
ShieldedPy {
    int visible{1};

protected:
    int guarded{5};

    int peek() const {
        return guarded;
    }
};

// weld_protected composes with opt_in: protected members become visible, and
// then need the same explicit include as public ones
struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::opt_in,
  =welder::policy::weld_protected
]]
OptInShielded {
    [[=welder::mark::include]]
    int chosen{3};

protected:
    // opted in -> bound
    [[=welder::mark::include]]
    int picked{4};

    // visible to the resolution but not opted in -> not bound
    int unpicked{5};
};

} // namespace resolution

inline void register_resolution(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "resolution")};
    WELDER_TEST_WELDER::weld_namespace<^^resolution>(sub);
}
