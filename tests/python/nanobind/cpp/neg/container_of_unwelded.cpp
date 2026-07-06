// Negative-compile case (must FAIL to build): the bindability check is
// exhaustive — it recurses into container element types. A std::vector<Unwelded>
// field is bindable only if Unwelded is, so binding a type that exposes one is a
// hard error even though std::vector itself has a native nanobind caster (its
// <nanobind/stl/vector.h> converter is included below). Proves the recursion
// catches an unwelded type hidden inside a container. nanobind counterpart of
// tests/pybind11/cpp/neg/container_of_unwelded.cpp.
//
// Built by the `negcompile.nanobind.container_of_unwelded` CTest, which expects
// failure.
#include <vector>

#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>

#include <welder/rods/python/nanobind/rod.hpp>

struct Unwelded {  // deliberately not welded
    int v{0};
};

struct [[=welder::weld(welder::lang::py)]] Bag {
    std::vector<Unwelded> items;
};

void bind_it(nanobind::module_& m) { welder::welder<welder::rods::nanobind::rod>::weld_type<Bag>(m); }
