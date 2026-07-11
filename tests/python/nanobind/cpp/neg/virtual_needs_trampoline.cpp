// Negative-compile case (must FAIL to build): a welded type with an overridable
// virtual method must register a trampoline (welder::rods::python::trampoline_for)
// so a Python subclass can override it, or opt out with
// [[=welder::rods::python::bind_flat]]. With neither, welder's Python rod refuses to
// bind it — silently non-overridable polymorphism is a footgun, not a default.
//
// Built by the `negcompile.nanobind.virtual_needs_trampoline` CTest, which expects
// failure. The TU is otherwise valid, so the only thing that can fail is the
// trampoline-gate static_assert in make_class.
#include <string>

#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <welder/rods/python/nanobind/rod.hpp>
#include <welder/rods/python/nanobind/trampoline.hpp>

struct [[=welder::weld(welder::lang::py)]] Beast {
    virtual ~Beast() = default;
    virtual std::string roar() const { return "rawr"; } // overridable, unrouted
};

void bind_it(nanobind::module_& m) {
    welder::welder<welder::rods::nanobind::rod<>>::weld_type<Beast>(m);
}