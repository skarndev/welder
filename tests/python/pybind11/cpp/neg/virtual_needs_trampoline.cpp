// Negative-compile case (must FAIL to build): a welded type with an overridable
// virtual method must register a trampoline (welder::rods::python::trampoline_for)
// so a Python subclass can override it, or opt out with
// [[=welder::rods::python::bind_flat]]. With neither, welder's Python rod refuses to
// bind it — silently non-overridable polymorphism is a footgun, not a default.
// pybind11 counterpart of tests/python/nanobind/cpp/neg/virtual_needs_trampoline.cpp.
//
// Built by the `negcompile.pybind11.virtual_needs_trampoline` CTest, which expects
// failure. The TU is otherwise valid, so the only thing that can fail is the
// trampoline-gate static_assert in make_class.
#include <string>

#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <welder/rods/python/pybind11/rod.hpp>
#include <welder/rods/python/pybind11/trampoline.hpp>

struct [[=welder::weld(welder::lang::py)]] Beast {
    virtual ~Beast() = default;
    virtual std::string roar() const { return "rawr"; } // overridable, unrouted
};

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_type<Beast>(m);
}