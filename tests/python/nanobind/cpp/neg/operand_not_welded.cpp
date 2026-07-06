// Negative-compile case (must FAIL to build): a member operator whose operand
// type is not welded cannot be bound — nanobind has no converter for RawTag, so
// the dunder would be dead AND its stub would reference an unimportable type.
// welder's bindability gate turns this into a hard error. nanobind counterpart of
// tests/pybind11/cpp/neg/operand_not_welded.cpp.
//
// Built by the `negcompile.nanobind.operand_not_welded` CTest, which expects
// failure. The TU is otherwise valid, so the only thing that can fail is the
// bindability static_assert.
#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>

#include <welder/rods/python/nanobind/rod.hpp>

struct RawTag {  // deliberately not welded
    int id{0};
};

struct [[=welder::weld(welder::lang::py)]] Tagged {
    int id{0};
    Tagged() = default;
    explicit Tagged(int i) : id{i} {}
    Tagged operator+(const RawTag& t) const { return Tagged{id + t.id}; }
};

void bind_it(nanobind::module_& m) { welder::welder<welder::rods::nanobind::rod>::weld_type<Tagged>(m); }
