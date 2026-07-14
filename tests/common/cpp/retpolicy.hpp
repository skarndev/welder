#pragma once
// Return-value policy + keep_alive — mirrors tests/python/test_retpolicy.py and
// tests/lua/spec/retpolicy_spec.lua.
//
// The point of the group is the Python/Lua asymmetry: the Python rods honor a
// [[=welder::return_policy]] (pybind11 return_value_policy / nanobind rv_policy),
// while the garbage-collected Lua runtimes decide ownership *structurally* from the
// C++ return type and so ignore the policy at runtime — exactly as they ignore
// [[=welder::doc]]. What no rod ignores is a contradiction (a reference to a
// returned temporary): that is a compile error everywhere (the negative-compile
// case cpp/neg/return_policy_dangling.cpp).
//
// The cases live in namespace `retpolicy`, bound under a `retpolicy` submodule.
// #included by bindings.cpp after the welder vocabulary + the active backend.

namespace retpolicy {

// A small bound value type; `view()`/`snapshot()` below hand one back under
// different policies so the difference is observable from the target language.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Inner {
    int v{0};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Owner {
    Owner() = default;

    // reference_internal: a live, non-owning view aliasing the owner's member —
    // writing through it writes the C++ object, and the owner is kept alive while
    // the view lives. (Lua: a structural reference, same observable aliasing.)
    [[=welder::return_policy(welder::rv::reference_internal)]]
    Inner& view() { return inner_; }

    // copy: an independent snapshot — writing through it leaves the owner
    // untouched. The Lua rods ignore the policy (ownership is structural), so
    // there `snapshot()` is still a reference; the asymmetry is deliberate and the
    // Python-only behavior is asserted only on the Python side.
    [[=welder::return_policy(welder::rv::copy)]]
    Inner& snapshot() { return inner_; }

    // Read the owner's member back, to observe whether a write reflected.
    int inner_v() const { return inner_.v; }

  private:
    Inner inner_{}; // private: not a bound member, reached only via the accessors
};

// keep_alive is a pure Python-binding call policy (both Python frameworks have it;
// the Lua runtimes do not), so `Registry` is welded for Python only. `track` keeps
// the passed Inner (index 2) alive as long as the registry (index 1, the implicit
// `this`) — without it, `del`-ing the Python Inner would free the object `first_`
// still points at.
struct
[[=welder::weld(welder::lang::py)]]
Registry {
    Registry() = default;

    [[=welder::keep_alive(1, 2)]]
    void track(Inner& i) { first_ = &i; }

    int first_v() const { return first_ ? first_->v : -1; }

  private:
    Inner* first_{nullptr};
};

} // namespace retpolicy

inline void register_retpolicy(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "retpolicy")};
    WELDER_TEST_WELDER::weld_namespace<^^retpolicy>(sub);
}