// Cookbook 06 — templates: weld class-template INSTANTIATIONS.
//
// Annotations on a template declaration are readable through every instantiation,
// so one annotated template yields as many bound types as you instantiate. An
// instantiation has no identifier of its own, so it needs a name from you — and
// there are two routes:
//
//   1. (recommended) a NAMESPACE-SCOPE ALIAS: `using IntBox = Box<int>;` rides the
//      weld_namespace sweep and binds under the alias's name — no name strings;
//   2. the direct route: weld_type<Box<double>>(m, "RealBox") with the explicit
//      name (a type template parameter dealiases, so no alias can help there).
//
// A function-template instantiation binds the direct way, formed with
// std::meta::substitute. docs/content/cookbook/templates.md walks through this file.
#include <string>
#include <utility>

#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/rod.hpp>

namespace boxes {

// The weld / doc / tparam annotations sit on the PRIMARY TEMPLATE and are
// inherited by every instantiation.
template <class T>
struct
[[
  =welder::weld(welder::lang::py),
  =welder::doc("A single value in a labelled box."),
  =welder::tparam("T", "the stored value type")
]]
Box {
    std::string label;
    T value{};

    Box() = default;
    Box(std::string l, T v) : label{std::move(l)}, value{std::move(v)} {}

    [[=welder::doc("The label, rendered with the value's presence.")]]
    std::string describe() const { return label + ": present"; }
};

// Route 1: alias-declared instantiations. members_of(ns) enumerates the template,
// never a specialization, so these aliases are how the instantiations enter the
// weld_namespace sweep below — each binds under its alias's name. The template's
// weld/doc gate and document them; the alias may add only weld / weld_as.
using IntBox = Box<int>;
using TextBox = Box<std::string>;

// A function template; an instantiation is formed below with substitute(). (The
// uninstantiated template is not a bindable entity — the sweep skips it.)
template <class T>
[[=welder::weld(welder::lang::py), =welder::doc("Swap the contents of two boxes.")]]
void swap_boxes(Box<T>& a, Box<T>& b) {
    std::swap(a.label, b.label);
    std::swap(a.value, b.value);
}

} // namespace boxes

PYBIND11_MODULE(boxes, m) {
    m.doc() = "welder cookbook 06 - binding template instantiations";
    using weld = welder::welder<welder::rods::pybind11::rod<>>;
    // Route 1: the sweep binds the alias-declared instantiations (IntBox, TextBox).
    weld::weld_namespace<^^boxes>(m);
    // Route 2: a directly-welded instantiation. The explicit name is REQUIRED here:
    // Box<double> has no identifier, and the alias trick cannot reach weld_type (a
    // type template parameter dealiases before welder sees it).
    weld::weld_type<boxes::Box<double>>(m, "RealBox");
    // A function-template instantiation, reflected via substitute().
    weld::weld_function<std::meta::substitute(^^boxes::swap_boxes, {^^int})>(
        m, "swap_int_boxes");
}