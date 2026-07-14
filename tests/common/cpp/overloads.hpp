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
// Constructors resolve SYMMETRICALLY with every other member (opt_in binds only
// marked-include ctors), with two fail-safes locked below: the implicit/declared
// default constructor stays outside opt_in's default-out (you cannot `include`
// an implicit ctor) while its explicit marks are honored, and filtering that
// would leave a type with no constructor at all is a hard compile error unless
// the emptiness is explicit (negcompile.optin_uninstantiable; FactoryOnly shows
// the explicit mark::exclude escape).
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

// opt_in binds only marked members — constructors included (symmetric). The
// default constructor is exempt from the default-out (an implicit one has no
// declaration to mark), so the type stays default-instantiable.
struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::opt_in
]]
OptInCtor {
    OptInCtor() = default; // default ctor: outside opt_in's default-out

    OptInCtor(int seed) : kept{seed} {} // unmarked -> NOT bound under opt_in

    [[=welder::mark::include]]
    OptInCtor(int a, int b) : kept{a + b} {} // included -> bound

    [[=welder::mark::include]]
    int kept{0};

    int hidden{0}; // not included -> not bound
};

// The explicit factory-only escape: mark::exclude on every constructor zeroes
// the guard's automatic baseline, so "no constructor at all" compiles as a
// deliberate surface (instances arrive from C++ only).
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
FactoryOnly {
    [[=welder::mark::exclude]]
    FactoryOnly(int tag) : id{tag} {}

    int id{0};
};

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline FactoryOnly forge(int tag) { return FactoryOnly{tag}; }

// A DECLARED default constructor's explicit marks are honored: T() is
// suppressed while the value constructor stays.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
NoDefault {
    [[=welder::mark::exclude]]
    NoDefault() = default;

    NoDefault(int seed) : v{seed} {}

    int v{0};
};

} // namespace overloads

inline void register_overloads(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "overloads")};
    WELDER_TEST_WELDER::weld_namespace<^^overloads>(sub);
}