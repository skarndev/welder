#pragma once
// Opaque containers via the GENERATOR (welder::rods::opaque_containers) — mirrors
// tests/test_gen_opaque.py. Unlike opaque.hpp (which hand-writes the WELDER_OPAQUE
// declarations + welded aliases), here NONE of that boilerplate is written: the
// welded types below carry plain std::vector / std::map MEMBERS and signatures, and
// the generator reflects them and emits gen_opaque.opaque.hpp (the WELDER_OPAQUE
// declarations + aliases), which both Python bindings.cpp #include before the module.
// So this group is a self-test of the generator rod AND a cross-backend consistency
// check (one generated header, both extensions).
//
// A [[=welder::rods::python::by_value]] member proves the opt-out (stays a list).
//
// Container element/key types are chosen DISJOINT from the by-value stl.hpp group
// (vector<int>, map<string,int>, …) and the hand-written opaque.hpp group
// (vector<float>, map<int,string>, …) so the generator's module-wide WELDER_OPAQUE
// does not clobber either in the shared translation unit. (vector<int> appears only
// on the by_value member, which the generator excludes — so it stays by-value, exactly
// as stl.hpp uses it.)
//
// Python-only, like stl.hpp/opaque.hpp. #included after the welder vocabulary + backend.
#include <string>
#include <vector>

#include <welder/rods/python/opaque_containers/marks.hpp> // by_value opt-out

namespace gen_opaque {

// A welded ELEMENT type. Thanks to the driver's two-phase sweep (its name is
// predeclared before the containers bind), std::vector<Reading> is now opened OPAQUE
// by the generator — as a member, as an aggregate-NSDMI field, and as a function
// return — with clean stubs (no raw C++ name).
struct [[=welder::weld(welder::lang::py)]] Reading {
    double value{0.0};

    Reading() = default;
    Reading(double v) : value{v} {}
};

// A welded class TEMPLATE, its instantiation welded (and named) through an alias — a
// std::vector of it exercises derive_name for a class-template-specialization element
// with an NTTP argument. This used to produce an INVALID alias name (keeping `< > ::`);
// it must now be a valid identifier (VectorLayer0) and bind.
template <int N>
struct [[=welder::weld(welder::lang::py)]] Layer {
    int depth{N};
};
using Layer0 [[=welder::weld(welder::lang::py)]] = Layer<0>;

struct [[=welder::weld(welder::lang::py)]] Series {
    std::vector<double> points{};              // auto opaque -> VectorDouble (buffer)
    std::vector<Reading> readings{};           // auto opaque -> VectorReading (class elem)
    std::vector<Layer<0>> tiers{};             // NTTP-template element -> VectorLayer0
    // opt-out: stays a plain list[int], no WELDER_OPAQUE emitted for vector<int>
    [[=welder::rods::python::by_value]] std::vector<int> raw{};
};
// NOTE: the generator's map handling (derive_name / spelling for std::map) is locked
// by tests/core/opaque_containers.cpp rather than a runtime bind_map member here —
// pybind11-stubgen mis-qualifies bind_map view types (ItemsView/…) across submodules,
// and this group shares a module with opaque.hpp's maps; opaque.hpp covers bind_map at
// runtime, and the generator's map naming is a pure consteval, locked at compile time.

// A container of a welded class, surfaced through a signature (return type).
[[=welder::weld(welder::lang::py)]]
std::vector<Reading> take(int n) {
    std::vector<Reading> out{};
    for (int i{0}; i < n; ++i)
        out.emplace_back(static_cast<double>(i));
    return out;
}

// Round-trip helper: read the C++-side vector back after Python mutates it.
[[=welder::weld(welder::lang::py)]]
double sum_points(const Series& s) {
    double total{0.0};
    for (double x : s.points)
        total += x;
    return total;
}

} // namespace gen_opaque

// Guarded like templates.hpp: the generator TU (gen_opaque_gen.cpp) includes this
// header to REFLECT the types but defines no WELDER_TEST_* macros, so the register
// hook (which needs them) is compiled only into the binding TUs.
#ifdef WELDER_TEST_MODULE_T
inline void register_gen_opaque(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "gen_opaque")};
    WELDER_TEST_WELDER::weld_namespace<^^gen_opaque>(sub);
}
#endif
