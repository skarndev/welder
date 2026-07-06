#pragma once
#include <span>
#include <string>

#include <welder/doc.hpp> // function_doc / param_doc / doc_style concept

/** @file
    Docstring styles shared by welder's Python backends.

    The language-agnostic documentation layer (`<welder/doc.hpp>`) reads the
    `[[=welder::doc(...)]]` / `returns` / parameter annotations off a reflected
    function and hands the raw pieces (@ref welder::function_doc) to a *style* — a
    type satisfying @ref welder::doc_style — which folds them into one docstring.
    This header holds the styles the Python backends (pybind11, nanobind, …) share,
    so neither re-derives how a Python docstring reads. A backend picks a style when
    it calls @ref welder::function_docstring.

    Requires the welder vocabulary to be available first (via `import welder;` or
    `#include <welder/vocabulary.hpp>`), like the rest of the reflection layer.
*/

namespace welder::rods::python {

/** Google-style docstring assembly.

    The summary, then an `Args:` block listing each *documented* parameter
    (`    name: text`), then a `Returns:` block. Undocumented parameters are
    omitted; the `Args:`/`Returns:` blocks are dropped entirely when empty, and
    blocks are separated from preceding content by a blank line. A multiline
    param/returns doc keeps its continuation lines indented under the block, so
    docstrings carrying multiline examples stay readable.

    This is the default style both Python backends pass to
    @ref welder::function_docstring; it satisfies @ref welder::doc_style.
*/
struct google_style {
    /** Assemble @a d into a Google-style docstring.
        @param d the documentation pieces.
        @return the formatted docstring (possibly empty). */
    static std::string format(const ::welder::function_doc& d) {
        std::string out{};
        // Separate a new block from preceding content by exactly one blank line,
        // whether or not that content already ended in a newline (the Args block
        // leaves a trailing one per parameter line; the summary does not).
        auto blank_line = [&out]() {
            if (!out.empty()) {
                if (out.back() != '\n')
                    out += '\n';
                out += '\n';
            }
        };
        // Append `text`, indenting every continuation line by `indent` so a
        // multiline param/returns doc stays under its block (docs often carry
        // examples spanning lines). A trailing newline gets no indent (no
        // dangling whitespace line).
        auto append_indented = [&out](const char* text, const char* indent) {
            for (const char* c{text}; *c; ++c) {
                out += *c;
                if (*c == '\n' && c[1] != '\0')
                    out += indent;
            }
        };

        if (d.summary)
            out += d.summary;

        bool any_param_doc{false};
        for (const auto& p : d.params)
            if (p.text) {
                any_param_doc = true;
                break;
            }
        if (any_param_doc) {
            blank_line();
            out += "Args:\n";
            for (const auto& p : d.params)
                if (p.text) {
                    out += "    ";
                    out += p.name ? p.name : "?";
                    out += ": ";
                    append_indented(p.text, "        ");
                    out += '\n';
                }
        }

        if (d.returns) {
            blank_line();
            out += "Returns:\n    ";
            append_indented(d.returns, "    ");
        }
        return out;
    }
};

static_assert(::welder::doc_style<google_style>);

} // namespace welder::rods::python
