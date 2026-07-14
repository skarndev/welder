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

// One hatch is needed: vecmath::Vec3 appears in signatures (dot, cross, ...),
// and the bindability gate proves a class type representable via its weld marker
// — which a third-party type doesn't have. The greedy pass below DOES register
// Vec3, so vouch for it with the type-level trust_bindable hatch (the same hatch
// you'd use for a hand-registered type).
template <>
inline constexpr bool welder::trust_bindable<vecmath::Vec3> = true;

PYBIND11_MODULE(fastvec, m) {
    m.doc() = "welder cookbook 08 - tack-welding an unannotated library";
    using tack = welder::welder<welder::rods::pybind11::rod<>,
                                welder::naming::none,
                                welder::tack_welding_carriage>;
    tack::weld_namespace<^^vecmath>(m);
}