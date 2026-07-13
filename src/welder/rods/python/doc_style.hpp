#pragma once
#include <span>
#include <string>
#include <string_view>

#include <welder/doc.hpp> // detail::function_doc / detail::param_doc / doc_style concept

/** @file
    Docstring styles shared by welder's Python backends.

    The language-agnostic documentation layer (`<welder/doc.hpp>`) reads the
    `[[=welder::doc(...)]]` / `returns` / parameter annotations off a reflected
    function and hands the raw pieces (@ref welder::detail::function_doc) to a *style* — a
    type satisfying @ref welder::doc_style — which folds them into one docstring.
    This header holds the styles the Python backends (pybind11, nanobind, …) share,
    so neither re-derives how a Python docstring reads. A backend selects one via
    its `DocStyle` template parameter (`welder::rods::pybind11::basic_rod<Style>`),
    which it passes to @ref welder::function_docstring; the default is
    @ref welder::rods::python::google_style.

    Three conventions are provided, matching the three docstring dialects the Python
    ecosystem's doc tools understand:
    - @ref welder::rods::python::google_style — Google (`Args:` / `Returns:` blocks; Napoleon-parsed);
    - @ref welder::rods::python::numpy_style  — NumPy / numpydoc (underlined `Parameters` / `Returns`
      sections; also Napoleon-parsed);
    - @ref welder::rods::python::sphinx_style — Sphinx / reStructuredText (`:param name:` / `:returns:`
      field lists, the format `sphinx-autodoc` emits natively).

    Every style's `format` is **`constexpr`**: it is a plain `std::string`
    assembly, unit-testable by `static_assert` the same way
    @ref welder::cleandoc is, and usable in any future compile-time context. That
    is also why the assembly is hand-rolled rather than written with `std::format`
    — `std::format` is not `constexpr` in the standard library (as of gcc-16), so a
    `constexpr` docstring builder cannot call it.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`), like
    the rest of the reflection layer.
*/

namespace welder::rods::python {

namespace detail {

/** Append text to @a out, indenting every *continuation* line by @a indent so a
    multiline param/returns doc stays visually under its block (docs often carry
    examples spanning lines). A trailing newline gets no indent, so no dangling
    whitespace line is left. Shared by all three Python styles. */
constexpr void append_indented(std::string& out, const char* text,
                               std::string_view indent) {
    for (const char* c{text}; *c; ++c) {
        out += *c;
        if (*c == '\n' && c[1] != '\0')
            out += indent;
    }
}

/** Separate a new block from preceding content in @a out by exactly one blank
    line, whether or not that content already ended in a newline (a summary does
    not; a section body typically does). A no-op when @a out is still empty. */
constexpr void blank_line(std::string& out) {
    if (!out.empty()) {
        if (out.back() != '\n')
            out += '\n';
        out += '\n';
    }
}

/** Whether @a d has at least one documented parameter (so a params block is worth
    opening). An undocumented parameter contributes nothing to any style. */
constexpr bool any_param_doc(const ::welder::detail::function_doc& d) {
    for (const auto& p : d.params)
        if (p.text)
            return true;
    return false;
}

} // namespace detail

/** Google-style docstring assembly (the default).

    The summary, then an `Args:` block listing each *documented* parameter
    (`    name: text`), then a `Returns:` block. Undocumented parameters are
    omitted; the `Args:`/`Returns:` blocks are dropped entirely when empty, and
    blocks are separated from preceding content by a blank line. A multiline
    param/returns doc keeps its continuation lines indented under the block, so
    docstrings carrying multiline examples stay readable.

    This is the style both Python backends default to (their `DocStyle` template
    parameter); it satisfies @ref welder::doc_style. Google docstrings are what
    Sphinx's Napoleon extension parses out of the box.
*/
struct google_style {
    /** Assemble @a d into a Google-style docstring.
        @param d the documentation pieces.
        @return the formatted docstring (possibly empty). */
    static constexpr std::string format(const ::welder::detail::function_doc& d) {
        std::string out{};
        if (d.summary)
            out += d.summary;

        if (detail::any_param_doc(d)) {
            detail::blank_line(out);
            out += "Args:\n";
            for (const auto& p : d.params)
                if (p.text) {
                    out += "    ";
                    out += p.name ? p.name : "?";
                    out += ": ";
                    detail::append_indented(out, p.text, "        ");
                    out += '\n';
                }
        }

        if (d.returns) {
            detail::blank_line(out);
            out += "Returns:\n    ";
            detail::append_indented(out, d.returns, "    ");
        }
        return out;
    }
};

static_assert(::welder::doc_style<google_style>);

/** NumPy-style docstring assembly (numpydoc).

    The summary, then an underlined `Parameters` section listing each *documented*
    parameter (the parameter name on its own line, its doc indented four spaces
    beneath), then an underlined `Returns` section (the return doc indented four
    spaces). welder has no target-language type text to place after the numpydoc
    ``name : type`` colon, so the type is omitted — a form numpydoc accepts (a bare
    ``name`` followed by an indented description). Empty sections are dropped and
    each section is preceded by a blank line; multiline docs keep their
    continuation lines under the four-space body indent.

    Satisfies @ref welder::doc_style. NumPy docstrings are also parsed by Sphinx's
    Napoleon extension.
*/
struct numpy_style {
    /** Assemble @a d into a NumPy-style docstring.
        @param d the documentation pieces.
        @return the formatted docstring (possibly empty). */
    static constexpr std::string format(const ::welder::detail::function_doc& d) {
        std::string out{};
        // Append an underlined section header (`title` then a rule of matching
        // length), separated from prior content by a blank line.
        auto section = [&out](std::string_view title) {
            detail::blank_line(out);
            out += title;
            out += '\n';
            out.append(title.size(), '-');
            out += '\n';
        };

        if (d.summary)
            out += d.summary;

        if (detail::any_param_doc(d)) {
            section("Parameters");
            for (const auto& p : d.params)
                if (p.text) {
                    out += p.name ? p.name : "?";
                    out += "\n    ";
                    detail::append_indented(out, p.text, "    ");
                    out += '\n';
                }
        }

        if (d.returns) {
            section("Returns");
            detail::append_indented(out, d.returns, "");
        }
        return out;
    }
};

static_assert(::welder::doc_style<numpy_style>);

/** Sphinx-style docstring assembly (reStructuredText field lists).

    The summary, then a `:param name: text` field for each *documented* parameter,
    then a `:returns: text` field — the reST field-list form
    `sphinx.ext.autodoc` reads without any Napoleon translation. The field block is
    separated from the summary by a blank line; continuation lines of a multiline
    doc are indented four spaces so they stay part of the field body. Undocumented
    parameters are omitted; with no docs at all the result is empty.

    Satisfies @ref welder::doc_style.
*/
struct sphinx_style {
    /** Assemble @a d into a Sphinx (reStructuredText) docstring.
        @param d the documentation pieces.
        @return the formatted docstring (possibly empty). */
    static constexpr std::string format(const ::welder::detail::function_doc& d) {
        std::string out{};
        if (d.summary)
            out += d.summary;

        const bool any_field{detail::any_param_doc(d) || d.returns};
        if (any_field)
            detail::blank_line(out);

        for (const auto& p : d.params)
            if (p.text) {
                out += ":param ";
                out += p.name ? p.name : "?";
                out += ": ";
                detail::append_indented(out, p.text, "    ");
                out += '\n';
            }

        if (d.returns) {
            out += ":returns: ";
            detail::append_indented(out, d.returns, "    ");
        }
        return out;
    }
};

static_assert(::welder::doc_style<sphinx_style>);

} // namespace welder::rods::python