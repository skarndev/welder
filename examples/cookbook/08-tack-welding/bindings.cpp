// Cookbook 08 — tack welding: bind an UNMARKED third-party library greedily.
//
// The default carriage (stitch welding) binds only where welder's markers direct.
// Swapping welder::tack_welding_carriage in as the third template argument flips
// the resolution: every reflectable type/function/variable participates, nested
// namespaces recurse greedily, and public bases are flattened — no weld markers
// needed anywhere. Bindability is STILL enforced (a non-representable member is
// a compile error; hatch with trust_bindable), and any mark::exclude that does
// exist is still honored. docs/content/cookbook/tack-welding.md walks through
// this file.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/rod.hpp>

#include "vecmath.hpp" // the "third-party" header — zero welder annotations

// Note vecmath::Vec3 appears in signatures (dot, cross, ...) without carrying a
// weld marker: the tack carriage's registration oracle accepts class types its
// own greedy pass registers, so no trust_bindable hatch is needed for the
// library's own types. (A type you never tack-weld — or a forward declaration —
// still trips the bindability gate.)

PYBIND11_MODULE(fastvec, m) {
    m.doc() = "welder cookbook 08 - tack-welding an unannotated library";
    using tack = welder::welder<welder::rods::pybind11::rod<>,
                                welder::naming::none,
                                welder::tack_welding_carriage>;
    tack::weld_namespace<^^vecmath>(m);
}