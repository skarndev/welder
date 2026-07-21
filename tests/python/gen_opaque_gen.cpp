// Opaque-container generator executable for the shared gen_opaque cases. Reflects the
// welded types (their std::vector / std::map members and signatures) and emits the
// backend-neutral header of WELDER_OPAQUE(...) declarations + welded aliases that both
// Python test extensions #include to bind those containers by reference. Built and run
// at build time by welder_generate_opaque_containers() (one generator, one header,
// shared by the pybind11 and nanobind modules).
#include <welder/vocabulary.hpp>
#include <welder/rods/python/opaque_containers/module.hpp> // rod + WELDER_OPAQUE_CONTAINERS_MAIN

#include "gen_opaque.hpp" // the welded types (register hook guarded out here)

WELDER_OPAQUE_CONTAINERS_MAIN(gen_opaque)
