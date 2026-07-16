// Negative-compile case (must FAIL to build): a weld mark on a union in a swept
// namespace is an explicit attempt to bind it — diagnosed loudly by the
// namespace walk rather than silently ignored. (An UNMARKED union in a swept
// namespace is skipped: tests/common/cpp/unions.hpp locks that side.)
//
// Built by the `negcompile.union_welded_in_namespace` CTest, which expects
// failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace catalog {

union [[=welder::weld(welder::lang::py)]] Payload {
    int i;
    float f;
};

} // namespace catalog

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^catalog>(m);
}