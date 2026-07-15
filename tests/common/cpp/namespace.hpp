#pragma once
// Namespace introspection — mirrors tests/test_namespace.py (same sections, same
// order). The whole C++ namespace `catalog` is exposed under a `catalog`
// submodule via WELDER_TEST_WELDER::weld_namespace.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.
#include <string>

namespace catalog {

// --- classes ----------------------------------------------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
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
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Animal2 {
    int legs{4};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Cat : public Animal2 {
    int whiskers{12};
};

// --- free functions ---------------------------------------------------------

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
int total(int a, int b) {
    return a + b;
}

// overload
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
int total(int a) {
    return a;
}

// no weld -> not exposed
int internal_helper(int x) {
    return x;
}

// Welded candidate, but excluded for Python -> resolved out, like a struct member.
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::mark::exclude(welder::lang::py)
]]
int suppressed() {
    return -1;
}

// --- variables become module attributes -------------------------------------

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline constexpr int LIMIT{100};

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline const std::string TAG{"catalog"};

// no weld -> not exposed
inline constexpr int PRIVATE_LIMIT{7};

// --- mutable variables become live properties -------------------------------

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline int counter{0};

// mutate the global from C++
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
void bump() {
    ++counter;
}

// --- nested namespaces ------------------------------------------------------

// -> submodule "sub"
namespace sub {

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
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
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
int candidate() {
    return 1;
}

// welded + included -> bound
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
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
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Gizmo {
    int g{9};
};

}

// not included -> not recursed
namespace omitted {

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
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
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Spy {
    int s{0};
};

}

} // namespace catalog

inline void register_namespace(WELDER_TEST_MODULE_T& m) {
    // A whole namespace, bound under a submodule to keep names tidy. (The local
    // must not be named `catalog`, or it would shadow the namespace in `^^`.)
    auto catalog_mod{WELDER_TEST_SUBMODULE(m, "catalog")};
    WELDER_TEST_WELDER::weld_namespace<^^catalog>(catalog_mod);
}

// Entities exercised only through the semi-manual route below. Kept in their own
// namespace that is *never* weld_namespace'd, so each binds exactly once (into the
// `manual` submodule) — no double-binding to trip stub generators, and no shared
// mutable state with the `catalog` cases.
namespace freestanding {

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
int scale(int x, int factor) {
    return x * factor;
}

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline constexpr int MANUAL_CONST{42};

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline int manual_counter{0};

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
void manual_bump() {
    ++manual_counter;
}

// Bound under a call-site name override, to check the verbatim name beats the
// entity's own identifier.
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
int renamable(int x) {
    return x + 1;
}

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline constexpr int RENAMABLE{99};

} // namespace freestanding

// A stand-in for a third-party library that carries NO welder markers — no `weld`,
// no `policy`, no marks anywhere. It is bound greedily by the tack-welding carriage
// (see register_foreign), which ignores the missing annotations but still enforces
// bindability (every member here is representable).
namespace foreign {

// NB: no forward declarations here on purpose. The greedy registration oracle is
// a pure predicate (not a visited-set), so a forward-declared-then-defined type
// PASSES the gate and binds — but members_of yields an entity at its FIRST
// declaration, so a `struct Coupler;` hoist would register Coupler before Widget
// and pybind11 (which renders docstrings at def time) would then spell Widget's
// name raw in Coupler's property docstrings, failing the stubgen gate. Same
// caveat as hand-written pybind11: declare types before signatures that use them.

struct Widget {
    int size{3};
    int doubled() const {
        return size * 2;
    }
    // The library's own class types in signatures: under greedy resolution these
    // pass the gate WITHOUT a trust_bindable hatch, because the same tack pass
    // registers them (counts_as_registered).
    Widget merged(const Widget& other) const { return Widget{size + other.size}; }
};

struct Coupler {
    Widget left{};
    Widget right{};
};

// Class-typed parameters + return, including the forward-referenced Coupler.
Coupler fuse(const Widget& a, const Widget& b) {
    return Coupler{a, b};
}

int add(int a, int b) {
    return a + b;
}

inline constexpr int VERSION{7};

// greedily recursed into a submodule; Gadget in a NESTED namespace is likewise
// accepted by the oracle when referenced from the enclosing one.
namespace nested {
struct Gadget {
    int id{5};
};
} // namespace nested

int gadget_id(const nested::Gadget& g) {
    return g.id;
}

} // namespace foreign

// A third-party library with a PROTECTED surface (think an extensible framework
// base) — still zero welder markers, so the annotation route is closed. The
// greedy resolution's WeldProtected knob is the blanket opt-in for the pass; the
// plain tack of `foreign` above keeps its public-only default.
namespace foreign_protected {

struct Panel {
    int frame() const {
        return trim() + width;
    }

  protected:
    int trim() const {
        return 10;
    }
    int width{4};

  private:
    int serial{123}; // out under EVERY knob/resolution
};

} // namespace foreign_protected

// The bound_into distinction: Meter declares a protected member; Display
// merely inherits it (Meter is unmarked, so its members FLATTEN onto Display's
// binding). A resolution hook keyed on bound_into admits the member into
// Display's binding while refusing it on Meter's own — the same declaring
// class, two different flattening targets (see register_foreign). Kept in its
// own namespace so foreign_protected's blanket knob doesn't mask the
// distinction.
namespace foreign_mixed {

struct Meter {
    int model() const {
        return 1;
    }

  protected:
    int reading() const {
        return 55;
    }
};

struct Display : Meter {};

} // namespace foreign_mixed

// The semi-manual route: bind a hand-picked function/variable directly onto a
// module, without welding the whole enclosing namespace. Mirrors what namespace
// binding does per member, but one entity at a time.
inline void register_freestanding(WELDER_TEST_MODULE_T& m) {
    auto manual_mod{WELDER_TEST_SUBMODULE(m, "manual")};
    WELDER_TEST_WELDER::weld_function<^^freestanding::scale>(manual_mod);
    WELDER_TEST_WELDER::weld_function<^^freestanding::manual_bump>(manual_mod);
    WELDER_TEST_WELDER::weld_variable<^^freestanding::MANUAL_CONST>(manual_mod);
    WELDER_TEST_WELDER::weld_variable<^^freestanding::manual_counter>(manual_mod);
    // A verbatim name override, given directly at the call site — takes precedence
    // over the identifier/weld_as/style name the entity would otherwise resolve to.
    WELDER_TEST_WELDER::weld_function<^^freestanding::renamable>(manual_mod, "renamed_fn");
    WELDER_TEST_WELDER::weld_variable<^^freestanding::RENAMABLE>(manual_mod, "RENAMED_CONST");
}

// A bound_into-keyed resolution for the foreign_mixed tack below: protected
// members are admitted only into Display's binding.
struct display_only_resolution : ::welder::carriages::greedy_resolution<> {
    static consteval bool protected_participates(std::meta::info, ::welder::lang,
                                                 std::meta::info bound_into) {
        return bound_into == ^^foreign_mixed::Display;
    }
};

// Tack welding: bind the unmarked `foreign` library greedily. The tack welder is the
// same rod + style as WELDER_TEST_WELDER, only with the tack-welding carriage swapped
// in — reachable backend-neutrally through the entry point's type aliases.
inline void register_foreign(WELDER_TEST_MODULE_T& m) {
    using tack = ::welder::welder<WELDER_TEST_WELDER::rod_type,
                                  WELDER_TEST_WELDER::name_style,
                                  ::welder::tack_welding_carriage>;
    auto foreign_mod{WELDER_TEST_SUBMODULE(m, "foreign")};
    tack::weld_namespace<^^foreign>(foreign_mod);

    // The protected-admitting tack: greedy_resolution's WeldProtected knob, the
    // blanket for a library that cannot carry policy::weld_protected. Private
    // stays out regardless — that boundary is not a knob.
    using tack_protected = ::welder::welder<
        WELDER_TEST_WELDER::rod_type, WELDER_TEST_WELDER::name_style,
        ::welder::carriages::basic_carriage<
            ::welder::carriages::greedy_resolution<true>>>;
    auto fp_mod{WELDER_TEST_SUBMODULE(m, "foreign_protected")};
    tack_protected::weld_namespace<^^foreign_protected>(fp_mod);

    // The bound_into-keyed tack: the hook admits protected members only when
    // they land on Display's binding. Meter::reading is admitted where it
    // FLATTENS onto Display (bound_into == ^^Display though parent_of(mem) ==
    // ^^Meter — the carriage threads the welded type, not the declaring class)
    // and refused on Meter's own binding.
    using tack_mixed = ::welder::welder<
        WELDER_TEST_WELDER::rod_type, WELDER_TEST_WELDER::name_style,
        ::welder::carriages::basic_carriage<display_only_resolution>>;
    auto fm_mod{WELDER_TEST_SUBMODULE(m, "foreign_mixed")};
    tack_mixed::weld_namespace<^^foreign_mixed>(fm_mod);
}
