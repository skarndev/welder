// Negative-compile case (must FAIL to build): tack welding's registration oracle
// accepts a class type the greedy pass itself registers — but a FORWARD-DECLARED
// (incomplete) type cannot be registered, so a signature naming one must still
// trip the bindability gate at compile time (the runtime would otherwise raise
// an unregistered-type error at call time).
//
// Built by the `negcompile.tack_incomplete_param` CTest, which expects failure.
// The TU is otherwise valid, so the only thing that can fail is the bindability
// static_assert.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace vendor {

struct Opaque; // declared, never defined — the greedy walk cannot register it

int probe(const Opaque& o); // signature names the incomplete type

inline int ok(int x) { return x; }

} // namespace vendor

void bind_it(pybind11::module_& m) {
    using tack = welder::welder<welder::rods::pybind11::rod<>,
                                welder::naming::none,
                                welder::tack_welding_carriage>;
    tack::weld_namespace<^^vendor>(m);
}