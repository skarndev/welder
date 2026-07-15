// Negative-compile case (must FAIL to build): only weld / weld_as may be attached
// to a namespace-scope alias — every other welder mark belongs on the class
// template, where it applies to all instantiations. A doc on the alias trips the
// carriage's alias_marks_admissible static_assert instead of being silently ignored.
//
// Built by the `negcompile.alias_forbidden_mark` CTest, which expects failure.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace neg_alias_mark {

template <class T>
struct [[=welder::weld(welder::lang::py)]] Crate {
    T item{};
};

using IntCrate [[=welder::doc("docs belong on the template")]] = Crate<int>;

} // namespace neg_alias_mark

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^neg_alias_mark>(m);
}