#pragma once
// Namespace introspection — mirrors tests/test_namespace.py (same sections, same
// order). The whole C++ namespace `catalog` is exposed under a `catalog`
// submodule via WELDER_TEST_BE::bind_namespace.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.
#include <string>

namespace catalog {

// --- classes ----------------------------------------------------------------

struct
[[=welder::weld(welder::lang::py)]]
Item {
    int id{0};

    Item() = default;
    Item(int i) : id{i} {}

    int get_id() const {
        return id;
    }
};

// no weld -> not exposed
struct Hidden {
    int x{0};
};

// A welded base declared before its welded derived (C++ requires this order),
// so namespace binding registers the base first and native inheritance works.
struct
[[=welder::weld(welder::lang::py)]]
Animal2 {
    int legs{4};
};

struct
[[=welder::weld(welder::lang::py)]]
Cat : public Animal2 {
    int whiskers{12};
};

// --- free functions ---------------------------------------------------------

[[=welder::weld(welder::lang::py)]]
int total(int a, int b) {
    return a + b;
}

// overload
[[=welder::weld(welder::lang::py)]]
int total(int a) {
    return a;
}

// no weld -> not exposed
int internal_helper(int x) {
    return x;
}

// Welded candidate, but excluded for Python -> resolved out, like a struct member.
[[
  =welder::weld(welder::lang::py),
  =welder::mark::exclude(welder::lang::py)
]]
int suppressed() {
    return -1;
}

// --- variables become module attributes -------------------------------------

[[=welder::weld(welder::lang::py)]]
inline constexpr int LIMIT{100};

[[=welder::weld(welder::lang::py)]]
inline const std::string TAG{"catalog"};

// no weld -> not exposed
inline constexpr int PRIVATE_LIMIT{7};

// --- mutable variables become live properties -------------------------------

[[=welder::weld(welder::lang::py)]]
inline int counter{0};

// mutate the global from C++
[[=welder::weld(welder::lang::py)]]
void bump() {
    ++counter;
}

// --- nested namespaces ------------------------------------------------------

// -> submodule "sub"
namespace sub {

struct
[[=welder::weld(welder::lang::py)]]
Nested {
    int v{5};
};

}

// no welded content -> not exposed
namespace quiet {

struct Plain {
    int p{0};
};

}

// opt_in namespace: only welded members that are *also* included bind, and a
// nested namespace is recursed only if it is explicitly included.
namespace
[[=welder::policy::opt_in]]
strict {

// welded, but not included -> skipped
[[=welder::weld(welder::lang::py)]]
int candidate() {
    return 1;
}

// welded + included -> bound
[[
  =welder::weld(welder::lang::py),
  =welder::mark::include(welder::lang::py)
]]
int chosen() {
    return 2;
}

// included -> recursed
namespace
[[=welder::mark::include(welder::lang::py)]]
shown {

struct
[[=welder::weld(welder::lang::py)]]
Gizmo {
    int g{9};
};

}

// not included -> not recursed
namespace omitted {

struct
[[=welder::weld(welder::lang::py)]]
Ghost {
    int x{0};
};

}

}

// A whole sub-namespace pruned for Python via mark::exclude.
namespace
[[=welder::mark::exclude(welder::lang::py)]]
secret {

struct
[[=welder::weld(welder::lang::py)]]
Spy {
    int s{0};
};

}

} // namespace catalog

inline void register_namespace(WELDER_TEST_MODULE_T& m) {
    // A whole namespace, bound under a submodule to keep names tidy. (The local
    // must not be named `catalog`, or it would shadow the namespace in `^^`.)
    auto catalog_mod{m.def_submodule("catalog")};
    WELDER_TEST_BE::bind_namespace<^^catalog>(catalog_mod);
}
