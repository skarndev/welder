#pragma once
// Per-overload / per-constructor marks — mirrors tests/python/test_overloads.py and
// tests/lua/spec/overloads_spec.lua.
//
// Every overload of a name (and every constructor) resolves INDEPENDENTLY through
// the marks: exclude one and its siblings still bind; exclude one per-language and
// the two languages see different sets. The carriage computes each name's
// participating overload GROUP and hands it to the rod whole, so the bound set is
// identical across all rods (a one-value-per-name Lua slot never silently swallows
// a pruned or surviving sibling).
//
// A type's constructors honor explicit marks, but NOT the opt_in policy default —
// `policy::opt_in` governs which members are exposed; constructibility is
// orthogonal (OptInCtor locks that: an opt_in type keeps its unmarked ctor).
//
// #included by bindings.cpp after the welder vocabulary + the active backend.
#include <string>

namespace overloads {

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Calc {
    int value{0};

    Calc() = default;
    Calc(int seed) : value{seed} {}

    // An excluded CONSTRUCTOR: this one never binds, its siblings above do.
    [[=welder::mark::exclude]]
    Calc(std::string, std::string) : value{-1} {}

    // The surviving overloads bind and dispatch (distinct parameter types, so a
    // non-matching call errors on every rod rather than coercing).
    int apply(int x) const { return value + x + 1; }

    // Excluded for Lua only: Python keeps the string-tagged form.
    [[=welder::mark::exclude(welder::lang::lua)]]
    std::string apply(const std::string& tag, int rep) const {
        return tag + ":" + std::to_string(value + rep);
    }

    // Excluded everywhere: bound nowhere.
    [[=welder::mark::exclude]]
    int apply(std::string, std::string) const { return -1; }
};

// Free-function overload sets resolve the same way.
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline int pick(int x) { return x; }

[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::mark::exclude(welder::lang::lua)
]]
inline std::string pick(const std::string& s) { return s + "!"; }

// opt_in exposes only marked members — but the unmarked constructor stays.
struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::opt_in
]]
OptInCtor {
    OptInCtor() = default;
    OptInCtor(int seed) : kept{seed} {} // unmarked, still constructible

    [[=welder::mark::include]]
    int kept{0};

    int hidden{0}; // not included -> not bound
};

} // namespace overloads

inline void register_overloads(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "overloads")};
    WELDER_TEST_WELDER::weld_namespace<^^overloads>(sub);
}