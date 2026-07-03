#pragma once
// Hostile corpus for the welder Doxygen INPUT_FILTER: deliberately broken,
// adversarial, partly not-even-C++ input (never compiled). The fail-safety
// contract under test: the filter exits 0, never crashes, transforms what it
// can and leaves verbatim what it cannot. expected_hostile.hpp golds the
// exact output; the run itself proves no exception escapes.

// (1) non-UTF-8 byte in a comment (Latin-1 café) - byte-exact passthrough
// (2) non-UTF-8 byte inside doc text - must survive into the comment
struct [[=welder::doc("café (Latin-1)")]] Cafe {};

// (3) adjacent ]] that is not an attribute close
inline int trap_subscript(int** m) { return m[0][m[1][0]]; }

// (4) foreign attribute carrying an arbitrary expression (welder-free block:
//     byte-identical, commas and all)
[[vendor::assume(f(a, (b + 1)) == g<int, 2>{}), gnu::aligned(16)]]
int expression_soup;

// (5) the same expression soup sharing a block with a welder doc
[[vendor::assume(f(a, (b + 1)) > 0), =welder::doc("kept sibling")]]
int expression_soup2;

// (6) namespace-alias spelling is *not* recognized: foreign, kept verbatim
[[=w::doc("alias - not ours")]]
int alias_spelled;

// (7) empty elements / trailing comma inside a block
[[=welder::weld(welder::lang::py),]]
int trailing_comma;

// (8) unbalanced parenthesis inside a block: unparseable -> block verbatim
[[=welder::doc("oops (never closed]]
int unbalanced;

// (9) unterminated raw string: from here on the file is literal-soup; the
//     annotation after it sits in broken territory - whatever the filter
//     does must be deterministic and crash-free (golded below)
const char* broken_raw = R"never(closed
[[=welder::doc("after the broken raw string")]]
int after_broken_raw;

// (10) unterminated [[ at end of file
struct [[=welder::weld(welder::lang::py) Dangling {};
