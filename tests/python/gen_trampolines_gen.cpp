// Trampoline-generator executable for the shared gen_trampolines cases. Emits the
// backend-neutral trampoline header both Python test extensions compile in. Built and
// run at configure/build time by welder_generate_trampolines() (one generator, one
// header, shared by the pybind11 and nanobind modules).
#include <welder/vocabulary.hpp>
#include <welder/rods/python/trampolines/module.hpp> // rod + WELDER_TRAMPOLINES_MAIN

#include "gen_trampolines.hpp" // the welded types (register hook guarded out here)

WELDER_TRAMPOLINES_MAIN(gen_trampolines)