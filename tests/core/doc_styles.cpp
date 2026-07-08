// welder Python docstring styles (compile-only; must SUCCEED to compile).
//
// The Python backends fold a function's summary + parameter docs + return doc
// into one docstring under a *style* (welder::rods::python::{google,numpy,sphinx}
// _style). Each style's format() is constexpr — a plain std::string assembly,
// unit-testable by static_assert exactly like welder::cleandoc — which is also why
// it is hand-rolled rather than written with std::format (std::format is not
// constexpr in the standard library). These static_asserts lock the exact layout
// each style emits; no Python is needed. Built as an EXCLUDE_FROM_ALL object
// library that must compile — same compile-to-pass shape as the other core tests.
#include <span>
#include <string_view>

#include <welder/vocabulary.hpp>               // annotation vocabulary (header form)
#include <welder/rods/python/doc_style.hpp>    // the three styles + function_doc

using namespace std::string_view_literals;
namespace py = welder::rods::python;

// A fixed set of documentation pieces the three styles are checked against: a
// summary, two documented parameters (the second spanning two lines), and a
// return doc. Static storage so the spans stay valid; constexpr so format() folds
// it at compile time.
constexpr welder::param_doc params[]{
    {"a", "the first operand"},
    {"b", "the second operand,\nspanning two lines"},
};
constexpr welder::function_doc full{
    "Combine two values.", std::span<const welder::param_doc>{params},
    "the combined result"};

// True iff Style::format(d) equals expected (compared in a consteval predicate so
// the transient std::string never escapes into the constant).
template <class Style>
consteval bool fmt_is(const welder::function_doc& d, std::string_view expected) {
    return Style::format(d) == expected;
}

// --- Google: `Args:` / `Returns:` blocks, continuation lines indented 8 ---------
static_assert(fmt_is<py::google_style>(
    full,
    "Combine two values.\n"
    "\n"
    "Args:\n"
    "    a: the first operand\n"
    "    b: the second operand,\n"
    "        spanning two lines\n"
    "\n"
    "Returns:\n"
    "    the combined result"sv));

// --- NumPy: underlined `Parameters` / `Returns`, name then indented body --------
static_assert(fmt_is<py::numpy_style>(
    full,
    "Combine two values.\n"
    "\n"
    "Parameters\n"
    "----------\n"
    "a\n"
    "    the first operand\n"
    "b\n"
    "    the second operand,\n"
    "    spanning two lines\n"
    "\n"
    "Returns\n"
    "-------\n"
    "the combined result"sv));

// --- Sphinx: reST `:param name:` / `:returns:` field list -----------------------
static_assert(fmt_is<py::sphinx_style>(
    full,
    "Combine two values.\n"
    "\n"
    ":param a: the first operand\n"
    ":param b: the second operand,\n"
    "    spanning two lines\n"
    ":returns: the combined result"sv));

// --- Undocumented function: every style yields the empty string -----------------
constexpr welder::function_doc empty{};
static_assert(fmt_is<py::google_style>(empty, ""sv));
static_assert(fmt_is<py::numpy_style>(empty, ""sv));
static_assert(fmt_is<py::sphinx_style>(empty, ""sv));

// --- Summary only (no params, no returns): the summary verbatim, no blocks ------
constexpr welder::function_doc summary_only{"Just a summary."};
static_assert(fmt_is<py::google_style>(summary_only, "Just a summary."sv));
static_assert(fmt_is<py::numpy_style>(summary_only, "Just a summary."sv));
static_assert(fmt_is<py::sphinx_style>(summary_only, "Just a summary."sv));

int main() {}