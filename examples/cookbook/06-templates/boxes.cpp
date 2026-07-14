// Cookbook 06 — templates: weld class-template INSTANTIATIONS.
//
// Annotations on a template declaration are readable through every instantiation,
// so one annotated template yields as many bound types as you instantiate — you
// pick the concrete types and the names (an instantiation has no identifier of
// its own, so the explicit weld_type name is REQUIRED). A function-template
// instantiation binds the same way, formed with std::meta::substitute.
// docs/content/cookbook/templates.md walks through this file.
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

// A function template; instantiations are formed below with substitute().
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
    // Each instantiation is welded like any type — the explicit name is required
    // (Box<int> has no identifier), and is used verbatim.
    weld::weld_type<boxes::Box<int>>(m, "IntBox");
    weld::weld_type<boxes::Box<std::string>>(m, "TextBox");
    // A function-template instantiation, reflected via substitute().
    weld::weld_function<std::meta::substitute(^^boxes::swap_boxes, {^^int})>(
        m, "swap_int_boxes");
}