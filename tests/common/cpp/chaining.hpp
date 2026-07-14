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

} // namespace chaining

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
}