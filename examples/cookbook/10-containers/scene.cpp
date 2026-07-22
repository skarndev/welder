// Cookbook 10 — the binding TU. Includes the welded types, then the GENERATED
// scene.opaque.hpp (the WELDER_OPAQUE declarations + welded aliases), then welds the
// namespace. No hand-written opaque boilerplate anywhere in this project.
#include <welder/vocabulary.hpp> // the annotation vocabulary, first

#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // the by_value layers vector still converts as a list

#include <welder/rods/python/pybind11/rod.hpp>

#include "scene.hpp"
#include "scene.opaque.hpp" // GENERATED — must follow scene.hpp + the backend rod

PYBIND11_MODULE(scene, m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^scene>(m);
}
