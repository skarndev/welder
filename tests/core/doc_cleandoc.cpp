// welder::cleandoc semantics (compile-only; must SUCCEED to compile).
//
// cleandoc is the docstring dedent applied at read time to every doc/returns/
// parameter text (via doc.hpp annotation_text_of), so a multiline doc can be
// indented to match the surrounding C++ source without that indentation reaching
// the target language's __doc__. It mirrors Python's inspect.cleandoc (PEP 257):
// strip the first line, remove the indentation common to the remaining non-blank
// lines, and trim leading/trailing blank lines — relative indentation is kept.
//
// The result is a std::string (non-transient in a constexpr variable), so the
// cases are checked inside a consteval predicate whose bool result is the
// constant; no allocation escapes. Built as an EXCLUDE_FROM_ALL object library
// that must compile — same compile-to-pass shape as the other core tests.
#include <string_view>

#include <welder/welder.hpp> // vocabulary + reflection (brings in doc.hpp)

using namespace std::string_view_literals;

// True iff cleandoc(in) equals expected.
consteval bool clean_is(std::string_view in, std::string_view expected) {
    return welder::cleandoc(in) == expected;
}

// 1. A single-line docstring is returned unchanged.
static_assert(clean_is("A circle."sv, "A circle."sv));

// 2. No leading/trailing whitespace, no indentation: identity (blank line kept).
static_assert(clean_is("Summary.\n\nMore."sv, "Summary.\n\nMore."sv));

// 3. First line abuts content, later lines share a 4-space indent: the first line
//    is stripped separately and the common margin is removed from the rest.
static_assert(clean_is("Combine two values.\n\n    Detailed multiline\n    description."sv,
                       "Combine two values.\n\nDetailed multiline\ndescription."sv));

// 4. The natural source style — opening on its own line, every line indented, a
//    trailing partially-indented line: dedented and end-trimmed.
static_assert(clean_is("\n    A gadget.\n\n    Description.\n  "sv,
                       "A gadget.\n\nDescription."sv));

// 5. Relative indentation is preserved: an example block indented *further* than
//    the prose keeps its extra indentation after the common margin is removed.
static_assert(clean_is("\n  A gadget.\n\n  Example:\n      >>> x\n      0\n  "sv,
                       "A gadget.\n\nExample:\n    >>> x\n    0"sv));

// 6. Leading and trailing blank lines are trimmed.
static_assert(clean_is("\n\nText.\n\n\n"sv, "Text."sv));

// 7. All-whitespace input collapses to empty.
static_assert(clean_is("   \n  \n"sv, ""sv));

// 8. The first line's indentation is stripped even when it is the only line.
static_assert(clean_is("    Indented lone line."sv, "Indented lone line."sv));

int main() {}
