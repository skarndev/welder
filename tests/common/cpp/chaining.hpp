#pragma once
// Handle-chaining cases — mirrors tests/python/test_chaining.py / chaining_spec.lua.
//
// weld_type returns the rod's class handle and weld_function the rod's bound
// function object (where the framework has one; see <welder/welder.hpp>). These
// cases prove hand-written framework registrations chain onto the returned
// handles — the mixing story: welder lays the boilerplate, bare backend code
// adds what welder shouldn't guess. Each backend's bindings TU supplies:
//   WELDER_TEST_CHAIN_CLASS_EXTRA(cls)   register a method `doubled` (returning
//                                        n * 2) on the returned class handle,
//                                        via the backend's own API
//   WELDER_TEST_CHAIN_FN_ALIAS(sub, fn)  (only when the rod returns a function
//                                        handle) expose `fn` again as
//                                        `twice_alias` on the submodule
namespace chaining {

struct [[=welder::weld(welder::lang::py, welder::lang::lua)]] Gadget {
    int n{0};
};

[[=welder::weld(welder::lang::py, welder::lang::lua)]]
inline int twice(int x) { return x + x; }

// Mixed overloads: non-template mix(int)/mix(string) alongside a MEMBER FUNCTION
// TEMPLATE of the same name. welder binds the non-template overloads as a group
// (a template is not a function — the walk skips it, silently); the backend seam
// then chains the instantiation mix<double> onto the returned handle under the
// same name, and it must JOIN the bound overload set. Python-only: pybind11 and
// nanobind accumulate same-named defs into one overloaded function; the Lua
// frameworks REPLACE same-key registrations, so the pattern is not portable
// there (their seam stays undefined and the type is welded for py alone).
struct [[=welder::weld(welder::lang::py)]] Mixer {
    std::string mix(int x) const { return "int:" + std::to_string(x); }
    std::string mix(const std::string& s) const { return "str:" + s; }

    template <class T>
    std::string mix(T v) const { return "tpl:" + std::to_string(v); }
};

} // namespace chaining

// The free-function analogue, in its own namespace so the GROUP binds via a sweep:
// weld_function cannot name `blend` at all here — `^^blend` is an overload set
// (ill-formed), and for the same reason substitute() cannot form blend<double>
// when the template shares its name with non-template overloads. The seam appends
// the instantiation with the framework's own module def.
namespace chaining_tpl_fns {

[[=welder::weld(welder::lang::py)]]
inline std::string blend(int x) { return "int:" + std::to_string(x); }
[[=welder::weld(welder::lang::py)]]
inline std::string blend(const std::string& s) { return "str:" + s; }

template <class T>
inline std::string blend(T v) { return "tpl:" + std::to_string(v); }

} // namespace chaining_tpl_fns

inline void register_chaining(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "chaining")};
    auto cls{WELDER_TEST_WELDER::weld_type<chaining::Gadget>(sub)};
    WELDER_TEST_CHAIN_CLASS_EXTRA(cls);
#ifdef WELDER_TEST_CHAIN_FN_ALIAS
    auto fn{WELDER_TEST_WELDER::weld_function<^^chaining::twice>(sub)};
    WELDER_TEST_CHAIN_FN_ALIAS(sub, fn);
#else
    // This rod's framework has no per-function handle; weld_function is void.
    WELDER_TEST_WELDER::weld_function<^^chaining::twice>(sub);
#endif
#ifdef WELDER_TEST_CHAIN_TPL_OVERLOAD
    // welder lays the non-template overload groups; the seam appends the
    // template instantiations under the same names (they must join the sets).
    auto mixer{WELDER_TEST_WELDER::weld_type<chaining::Mixer>(sub)};
    WELDER_TEST_WELDER::weld_namespace<^^chaining_tpl_fns>(sub);
    WELDER_TEST_CHAIN_TPL_OVERLOAD(sub, mixer);
#endif
}