// Cookbook 05 — the trampoline GENERATOR. A tiny build-time executable: it links
// only welder::trampolines (pure reflection + text, no pybind11/nanobind), reflects
// the welded virtual types in `machines`, and writes machines.trampolines.hpp —
// one backend-neutral trampoline subclass + trampoline_for registration per type.
// welder_generate_trampolines() (CMake) builds and runs it automatically.
#include <welder/vocabulary.hpp>
#include <welder/rods/python/trampolines/module.hpp> // rod + WELDER_TRAMPOLINES_MAIN

#include "machines.hpp"

WELDER_TRAMPOLINES_MAIN(machines)