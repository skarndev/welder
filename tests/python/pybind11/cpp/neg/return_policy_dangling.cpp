// Negative-compile case (must FAIL to build): a [[=welder::return_policy]] asking
// for a reference-category policy (reference / reference_internal) on a callable
// that returns *by value* would bind a reference to a temporary. welder rejects
// the contradiction at the bind site (welder::validate_return_policy) — in EVERY
// language, not just Python — so this is a hard error. This is the compile
// counterpart of the runtime cases in tests/python/test_retpolicy.py.
//
// Built by the `negcompile.return_policy_dangling` CTest, which expects failure.
// The TU is otherwise valid, so the only thing that can fail is the return-policy
// contradiction diagnostic.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py)]] Widget {
    Widget() = default;
    // reference to a prvalue -> dangles: the diagnosed contradiction.
    [[=welder::return_policy(welder::rv::reference)]]
    Widget make() const { return Widget{}; }
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Widget>(m);
}