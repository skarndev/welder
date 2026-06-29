#pragma once
// Namespace introspection — mirrors tests/test_namespace.py (same sections, same
// order). The whole C++ namespace `catalog` is exposed under a `catalog`
// submodule via welder::pybind11::bind_namespace.
//
// #included by bindings.cpp after the welder vocabulary + pybind11 backend.
#include <string>

namespace catalog {

// --- classes ----------------------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Item {
    int id{0};
    Item() = default;
    Item(int i) : id{i} {}
    int get_id() const { return id; }
};
struct Hidden { int x{0}; };                              // no weld -> not exposed

// A welded base declared before its welded derived (C++ requires this order),
// so namespace binding registers the base first and native inheritance works.
struct [[=welder::weld(welder::lang::py)]]
Animal2 { int legs{4}; };
struct [[=welder::weld(welder::lang::py)]]
Cat : public Animal2 { int whiskers{12}; };

// --- free functions ---------------------------------------------------------
[[=welder::weld(welder::lang::py)]] int total(int a, int b) { return a + b; }
[[=welder::weld(welder::lang::py)]] int total(int a) { return a; }   // overload
int internal_helper(int x) { return x; }                  // no weld -> not exposed
// Welded candidate, but excluded for Python -> resolved out, like a struct member.
[[=welder::weld(welder::lang::py)]] [[=welder::mark::exclude(welder::lang::py)]]
int suppressed() { return -1; }

// --- variables become module attributes -------------------------------------
[[=welder::weld(welder::lang::py)]] inline constexpr int LIMIT{100};
[[=welder::weld(welder::lang::py)]] inline const std::string TAG{"catalog"};
inline constexpr int PRIVATE_LIMIT{7};                    // no weld -> not exposed

// --- mutable variables become live properties -------------------------------
[[=welder::weld(welder::lang::py)]] inline int counter{0};
[[=welder::weld(welder::lang::py)]] void bump() { ++counter; } // mutate from C++

// --- nested namespaces ------------------------------------------------------
namespace sub {                                           // -> submodule "sub"
struct [[=welder::weld(welder::lang::py)]] Nested { int v{5}; };
}
namespace quiet {                                         // no welded content
struct Plain { int p{0}; };
}
// opt_in namespace: only welded members that are *also* included bind, and a
// nested namespace is recursed only if it is explicitly included.
namespace [[=welder::policy::opt_in]] strict {
[[=welder::weld(welder::lang::py)]] int candidate() { return 1; }   // welded, not included -> skipped
[[=welder::weld(welder::lang::py)]] [[=welder::mark::include(welder::lang::py)]]
int chosen() { return 2; }                                         // welded + included -> bound

namespace [[=welder::mark::include(welder::lang::py)]] shown {      // included -> recursed
struct [[=welder::weld(welder::lang::py)]] Gizmo { int g{9}; };
}
namespace omitted {                                                // not included -> not recursed
struct [[=welder::weld(welder::lang::py)]] Ghost { int x{0}; };
}
}
// A whole sub-namespace pruned for Python via mark::exclude.
namespace [[=welder::mark::exclude(welder::lang::py)]] secret {
struct [[=welder::weld(welder::lang::py)]] Spy { int s{0}; };
}

} // namespace catalog

inline void register_namespace(pybind11::module_& m) {
    // A whole namespace, bound under a submodule to keep names tidy. (The local
    // must not be named `catalog`, or it would shadow the namespace in `^^`.)
    auto catalog_mod{m.def_submodule("catalog")};
    welder::pybind11::bind_namespace<^^catalog>(catalog_mod);
}
