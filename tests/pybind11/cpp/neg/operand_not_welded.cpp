// Negative-compile case (must FAIL to build): a member operator whose operand
// type is not welded cannot be bound — pybind11 has no converter for RawTag, so
// the dunder would be dead AND its stub would reference an unimportable type.
// welder's bindability gate turns this into a hard error. This is the compile
// counterpart of tests/test_operators.py::test_operator_over_unwelded_operand_is_a_sharp_edge.
//
// Built by the `negcompile.operand_not_welded` CTest, which expects failure. The
// TU is otherwise valid, so the only thing that can fail is the bindability
// static_assert.
#include <welder/welder.hpp>

#include <pybind11/pybind11.h>

#include <welder/backends/pybind11.hpp>

struct RawTag {  // deliberately not welded
    int id{0};
};

struct [[=welder::weld(welder::lang::py)]] Tagged {
    int id{0};
    Tagged() = default;
    explicit Tagged(int i) : id{i} {}
    Tagged operator+(const RawTag& t) const { return Tagged{id + t.id}; }
};

void bind_it(pybind11::module_& m) { welder::pybind11::bind<Tagged>(m); }
