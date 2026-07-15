// Negative-compile case (must FAIL to build): two namespace-scope aliases welding
// the SAME class-template specialization would register it twice; the carriage's
// sole_alias_of_target static_assert diagnoses the duplicate at compile time
// instead of leaving it to the framework's import-time error.
//
// Built by the `negcompile.alias_duplicate` CTest, which expects failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace neg_alias_dup {

template <class T>
struct [[=welder::weld(welder::lang::py)]] Crate {
    T item{};
};

using IntCrate = Crate<int>;
using CrateOfInt = Crate<int>; // same specialization, second name -> diagnosed

} // namespace neg_alias_dup

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^neg_alias_dup>(m);
}