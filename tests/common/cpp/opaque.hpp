#pragma once
// Opaque, reference-semantic STL containers — mirrors tests/test_opaque.py (runtime)
// and the container cases in test_types.mypy-testing (type-level).
//
// The counterpart to stl.hpp: where that group binds containers BY VALUE (the
// frameworks' copy casters -> list/dict, a snapshot per access), this group binds
// them BY REFERENCE. The opt-in is a welded namespace-scope alias to the container
// (the template-instantiation mechanism), which routes to the rod's bind_container
// hook (py::bind_vector / py::bind_map, nb::bind_vector / nb::bind_map). Mutation
// then writes THROUGH to the C++ object, push_back shows up as append, and a scalar
// vector exposes its data() zero-copy to numpy.
//
// Python-only, like stl.hpp/doc.hpp: the Lua rods bind containers by reference
// structurally (userdata), so they need no opaque alias and do not include this group.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// A welded element type used by an opaque vector below — declared first so the opaque
// declaration can name std::vector<opaque::Point2D>.
namespace opaque {
struct [[=welder::weld(welder::lang::py)]] Point2D {
    double x{0.0};
    double y{0.0};
};
} // namespace opaque

// The frameworks select a container's caster by TYPE, module-wide, so opaqueness is a
// per-type decision at namespace scope, before the module. Each Python bindings.cpp
// defines WELDER_TEST_MAKE_OPAQUE(T) as its framework's opaque macro (WELDER_OPAQUE ->
// PYBIND11_MAKE_OPAQUE / NB_MAKE_OPAQUE). The element/key types below are chosen
// DISJOINT from the by-value stl.hpp group (vector<int>, map<string,int>, …) so making
// them opaque does not clobber that group's semantics in the same translation unit.
WELDER_TEST_MAKE_OPAQUE(std::vector<float>)
WELDER_TEST_MAKE_OPAQUE(std::vector<opaque::Point2D>)
WELDER_TEST_MAKE_OPAQUE(std::map<int, std::string>)
WELDER_TEST_MAKE_OPAQUE(std::unordered_map<std::string, int>)

namespace opaque {

// --- bind_vector containers (sequence) ---------------------------------------
// float is a contiguous scalar => also exposes the buffer protocol / an ndarray
// view (numpy zero-copy). A vector<Point2D> is contiguous but its element is a welded
// CLASS, not arithmetic, so it binds by reference (append/getitem/slicing) with NO
// buffer view — exercising the non-buffer branch of the contiguity+scalar gate, and
// that the element's own registration gates through (Point2D is welded).
using FloatVector [[=welder::weld(welder::lang::py)]] = std::vector<float>;
using PointList  [[=welder::weld(welder::lang::py)]] = std::vector<Point2D>;

// --- bind_map containers -----------------------------------------------------
using IntStrMap  [[=welder::weld(welder::lang::py)]] = std::map<int, std::string>;
using StrIntHash [[=welder::weld(welder::lang::py)]] = std::unordered_map<std::string, int>;

// A host whose opaque-vector member proves REFERENCE semantics: appending / assigning
// from Python writes through to the C++ object (the copy caster would snapshot).
struct [[=welder::weld(welder::lang::py)]] Signal {
    std::vector<float> samples{};
    // A `no_reassign` container member: still appendable/mutable IN PLACE (the
    // read-only binding hands out a live reference, so `s.locked.append(x)` writes
    // through), but rebinding the whole attribute — `s.locked = FloatVector()` —
    // raises AttributeError, exactly as a const member would, WITHOUT making the C++
    // member const (`samples`, above, is the reassignable control). This is the
    // motivating case: keep an opaque container appendable while forbidding a
    // whole-object assignment (which would otherwise expose an ugly mangled type).
    [[=welder::mark::no_reassign]] std::vector<float> locked{};
};

// Round-trip helpers: read a C++-side vector back after Python has mutated it, so a
// test can assert the mutation actually reached C++ (not a throwaway copy).
[[=welder::weld(welder::lang::py)]]
double sum_samples(const Signal& s) {
    double total{0.0};
    for (float x : s.samples)
        total += static_cast<double>(x);
    return total;
}

[[=welder::weld(welder::lang::py)]]
double sum_locked(const Signal& s) {
    double total{0.0};
    for (float x : s.locked)
        total += static_cast<double>(x);
    return total;
}

} // namespace opaque

inline void register_opaque(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "opaque")};
    WELDER_TEST_WELDER::weld_namespace<^^opaque>(sub);
}
