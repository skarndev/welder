// Negative-compile case (must FAIL to build): a registered trampoline must override
// *every* overridable virtual of the welded type. Here `size()` has no override, so
// a C++ call to it would never dispatch into a Python subclass — a silent gap.
// welder's coverage check (welder::rods::python::trampoline_covers) turns it into a
// hard error.
//
// Built by the `negcompile.nanobind.trampoline_missing_override` CTest, which expects
// failure. The TU is otherwise valid, so the only thing that can fail is the
// coverage static_assert in make_class.
#include <string>

#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <welder/rods/python/nanobind/rod.hpp>
#include <welder/rods/python/nanobind/trampoline.hpp>

struct [[=welder::weld(welder::lang::py)]] Beast {
    virtual ~Beast() = default;
    virtual std::string roar() const { return "rawr"; }
    virtual int size() const { return 9; }
};

struct PyBeast : Beast {
    WELDER_PY_TRAMPOLINE(PyBeast, Beast);
    std::string roar() const override { WELDER_PY_OVERRIDE(roar); }
    // MISSING: an override for size().
};

template <> constexpr std::meta::info
    welder::rods::python::trampoline_for<Beast> = ^^PyBeast;

void bind_it(nanobind::module_& m) {
    welder::welder<welder::rods::nanobind::rod<>>::weld_type<Beast>(m);
}