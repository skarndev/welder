// Cookbook 10 — the opaque-container generator: a three-line build-time executable.
// welder_generate_opaque_containers() builds and runs it, reflecting the welded types
// in namespace `scene` and emitting scene.opaque.hpp — the WELDER_OPAQUE declarations
// and welded aliases that bind every container it finds by reference. No numpy, no
// pybind11 here: pure reflection to text.
#include <welder/vocabulary.hpp>
#include <welder/rods/python/opaque_containers/module.hpp>

#include "scene.hpp"

WELDER_OPAQUE_CONTAINERS_MAIN(scene)
