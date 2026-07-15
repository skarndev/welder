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