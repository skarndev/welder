// Cookbook 05 — the binding TU. Identical in shape to recipe 04, except NO
// hand-written trampolines: the generated header supplies the PyT subclasses and
// the trampoline_for registrations. Include order matters — the active backend's
// trampoline.hpp (macros), then the types, then the generated header.
// docs/content/cookbook/generated-trampolines.md walks through this recipe.
#include <string>

#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/rod.hpp>
#include <welder/rods/python/pybind11/trampoline.hpp> // the neutral macros' pybind11 side

#include "machines.hpp"                // the welded virtual types
#include "machines.trampolines.hpp"    // GENERATED: trampolines + registrations

PYBIND11_MODULE(machines, m) {
    m.doc() = "welder cookbook 05 - generated trampolines";
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^machines>(m);
}