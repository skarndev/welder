#pragma once
/** @file
    welder's annotation vocabulary — the single header a consuming TU includes to
    get the markers it attaches to its types.

    welder ships header-only today, so this is *the* way to bring the vocabulary
    into scope (a C++20 `import welder;` module wrapper is planned but deferred —
    see the "Header-only for now" page in the docs). It provides the `lang`
    selectors and the annotation vocabulary (`weld` / `policy` / `mark` / `doc` /
    …). The reflection machinery and the `welder` binding entry point arrive with
    `<welder/welder.hpp>`, which every backend header (e.g.
    `<welder/rods/python/pybind11/rod.hpp>`) pulls in.
*/

#include <welder/version.hpp>
#include <welder/lang.hpp>
#include <welder/annotations.hpp>
