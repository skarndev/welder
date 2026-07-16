#pragma once
// Class-template instantiations welded through NAMESPACE-SCOPE ALIASES — mirrors
// tests/python/test_templates.py and tests/lua/spec/templates_spec.lua.
//
// members_of(ns) enumerates the class *template*, never an instantiation, so
// `using IntCrate = Crate<int>;` is the way an instantiation enters a
// weld_namespace sweep: the alias supplies both the C++ spelling and the default
// target-language name (no stringified name anywhere). Cases:
//   - a welded template with two DIFFERENT instantiations aliased (legal — only two
//     aliases of the SAME instantiation are diagnosed, see the neg tests);
//   - weld_as on the ALIAS (verbatim rename, most specific);
//   - weld_as on the TEMPLATE, applying to an alias that has none of its own;
//   - an UNWELDED (third-party-style) template opted in by a weld on the
//     alias-declaration itself.
//
// #included by each backend's bindings.cpp; welded for BOTH lang::py and lang::lua
// so all runtime rods bind the same surface (cross-rod consistency).

#include <string>

// A third-party-style template: carries NO weld mark of its own (think a vendor
// header you cannot annotate). The alias below opts one instantiation in.
namespace vendor_tpl {

template <class T>
struct Pack {
    // A nested type in the third-party template: it resolves under the
    // instantiation like any member (no weld anywhere on it), so the alias
    // opt-in below also brings IntPack.Lid along. NB: signatures NAMING Lid
    // (or Pack<T> itself) would not pass the stitch gate — the registration
    // oracle is a pure predicate of the declaration and cannot see a weld that
    // lives on a namespace-scope alias — so none do here; that is the
    // documented trust_bindable territory for alias-opt-in types.
    struct Lid {
        int fits{1};
    };

    T payload{};
    T unwrap() const { return payload; }
};

} // namespace vendor_tpl

namespace templates_ns {

template <class T>
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Crate {
    T item{};
    T get() const { return item; }
    void put(T v) { item = v; }
};

// Two different instantiations of one template, each under its alias's name.
using IntCrate = Crate<int>;
using WordCrate = Crate<std::string>;

// weld_as on the alias: bound verbatim as "CrateOfDouble" (the alias identifier is
// not used; the template's weld still gates participation).
using RenamedCrate [[=welder::weld_as("CrateOfDouble")]] = Crate<double>;

// weld_as on the TEMPLATE applies to an alias that has none of its own. (It names
// every instantiation of Tagged alike, so pair it with a single alias.)
template <class T>
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
[[=welder::weld_as("TaggedBox")]]
Tagged {
    T tag{};
};
using TaggedInt = Tagged<int>;

// The third-party opt-in: vendor_tpl::Pack is unwelded; the weld on the
// alias-declaration itself (annotations are legal there) takes precedence.
using IntPack
    [[=welder::weld(welder::lang::py, welder::lang::lua)]] = vendor_tpl::Pack<int>;

// NESTED types of an alias-welded instantiation: Silo<int>'s member class/enum
// resolve under the instantiation (the template's policy + their own marks, no
// weld of their own) and bind under the ALIAS's name — IntSilo.Hatch /
// IntSilo.State. Members whose signatures use them pass the gate: welded_for
// reads the TEMPLATE's weld through the instantiation, and the oracle's nested
// rule recurses into it.
template <class T>
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Silo {
    struct Hatch {
        int width{4};
    };
    enum class State { open, shut };

    T stored{};
    Hatch hatch{};

    State flip(State s) const {
        return s == State::open ? State::shut : State::open;
    }
};
using IntSilo = Silo<int>;

// policy::weld_protected on the TEMPLATE, read through the instantiation like
// every annotation: the alias-welded Vault<int> binds its protected members.
template <class T>
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
[[=welder::policy::weld_protected]]
Vault {
    T peek() const { return locked; }

protected:
    T locked{};
    void stash(T v) { locked = v; }

private:
    T combination{}; // never bound, weld_protected or not
};
using IntVault = Vault<int>;

} // namespace templates_ns

#ifdef WELDER_TEST_MODULE_T
inline void register_templates(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "templates")};
    WELDER_TEST_WELDER::weld_namespace<^^templates_ns>(sub);
}
#endif