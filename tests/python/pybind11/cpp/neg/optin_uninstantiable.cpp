// Negative-compile case (must FAIL to build): constructors resolve symmetrically
// under policy::opt_in, so a type whose constructors nobody marked — and which has
// no default constructor to fall back on — would silently become uninstantiable
// from Python. welder's no-constructor-left guard turns that into a hard error
// (the remedy in the message: mark::include a constructor, or mark them all
// exclude for an explicit factory-only surface — which negates the guard's
// automatic-policy baseline and compiles, see overloads.hpp FactoryOnly).
//
// Built by the `negcompile.optin_uninstantiable` CTest, which expects failure. The
// TU is otherwise valid, so the only thing that can fail is that static_assert.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct
[[=welder::weld(welder::lang::py), =welder::policy::opt_in]]
Locked {
    Locked(int seed) : v{seed} {} // unmarked -> filtered by opt_in; no default ctor

    [[=welder::mark::include]]
    int v{0};
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Locked>(m);
}