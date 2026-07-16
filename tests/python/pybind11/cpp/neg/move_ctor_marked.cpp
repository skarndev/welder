// Negative-compile case (must FAIL to build): an include/only mark on a MOVE
// constructor is a designed hard error. Move construction never crosses a
// language boundary — no target language has move semantics — so the move
// constructor is skipped structurally everywhere; a mark asking welder to bind
// it is an intent that cannot be honored (diag::marked_move_constructor names
// the copy constructor / __copy__ as what actually crosses).
//
// Built by the `negcompile.move_ctor_marked` CTest, which expects failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

struct [[=welder::weld(welder::lang::py)]] Snatched {
    Snatched() = default;

    [[=welder::mark::include]]
    Snatched(Snatched&&) noexcept = default;

    int n{0};
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Snatched>(m);
}