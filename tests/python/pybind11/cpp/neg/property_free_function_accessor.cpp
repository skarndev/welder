// Negative-compile case (must FAIL to build): a getter/setter mark on a
// NAMESPACE-SCOPE function — properties are a class surface, so the namespace
// walk diagnoses the mark instead of silently ignoring it (a static_assert in
// the carriage's function branch).
//
// Built by the `negcompile.property_free_function_accessor` CTest (WILL_FAIL).
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace acme {
[[=welder::weld(welder::lang::py), =welder::getter]]
inline int get_level() { return 3; }
} // namespace acme

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^acme>(m);
}
