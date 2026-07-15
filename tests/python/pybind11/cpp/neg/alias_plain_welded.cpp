// Negative-compile case (must FAIL to build): a namespace-scope alias to a welded
// NON-template type would bind the type twice — it already binds under its own
// name — so the carriage diagnoses it as a likely mistake (rename with weld_as
// instead of aliasing).
//
// Built by the `negcompile.alias_plain_welded` CTest, which expects failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace neg_alias_plain {

struct [[=welder::weld(welder::lang::py)]] Plain {
    int n{};
};

using PlainAgain = Plain; // the target is welded -> would register twice

} // namespace neg_alias_plain

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^neg_alias_plain>(m);
}