// Negative-compile case (must FAIL to build): the bindability check is
// exhaustive — it recurses into container element types. A std::vector<Unwelded>
// field is bindable only if Unwelded is, so binding a type that exposes one is a
// hard error even though std::vector itself has a native pybind11 caster. Proves
// the recursion catches an unwelded type hidden inside a container (the case the
// top-level needs_registration trait alone would miss).
//
// Built by the `negcompile.container_of_unwelded` CTest, which expects failure.
#include <vector>

#include <welder/welder.hpp>

#include <pybind11/pybind11.h>

#include <welder/backends/python/pybind11/backend.hpp>

struct Unwelded {  // deliberately not welded
    int v{0};
};

struct [[=welder::weld(welder::lang::py)]] Bag {
    std::vector<Unwelded> items;
};

void bind_it(pybind11::module_& m) { welder::pybind11::bind<Bag>(m); }
