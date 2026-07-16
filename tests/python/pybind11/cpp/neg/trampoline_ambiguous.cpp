// Negative-compile case (must FAIL to build): two [[=trampoline]]-annotated classes
// in the same namespace derive from the same welded base, and no trampoline_for<T>
// picks one — welder cannot know which is intended, so make_class's ambiguity
// static_assert rejects it (disambiguate by specializing trampoline_for<T>).
// pybind11 counterpart of tests/python/nanobind/cpp/neg/trampoline_ambiguous.cpp.
//
// Built by the `negcompile.trampoline_ambiguous` CTest, which expects failure.
#include <string>

#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <welder/rods/python/pybind11/rod.hpp>
#include <welder/rods/python/pybind11/trampoline.hpp>

struct [[=welder::weld(welder::lang::py)]] Beast {
    virtual ~Beast() = default;
    virtual std::string roar() const { return "rawr"; }
};

struct [[=welder::rods::python::trampoline]] PyBeastA : Beast {
    WELDER_PY_TRAMPOLINE(PyBeastA, Beast);
    std::string roar() const override { WELDER_PY_OVERRIDE(roar); }
};

struct [[=welder::rods::python::trampoline]] PyBeastB : Beast {
    WELDER_PY_TRAMPOLINE(PyBeastB, Beast);
    std::string roar() const override { WELDER_PY_OVERRIDE(roar); }
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Beast>(m);
}