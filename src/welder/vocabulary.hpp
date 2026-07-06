#pragma once
/** @file
    Header-only form of welder's annotation vocabulary.

    Include this (instead of `import welder;`) when you can't or don't want to use
    the module — e.g. to work around a compiler modules bug. It provides exactly
    what the module exports: the `lang` selectors and the annotation vocabulary
    (`weld` / `policy` / `mark` / `doc` / …). The reflection machinery and the
    `welder` binding entry point arrive with `<welder/welder.hpp>`, which every
    backend header (e.g. `<welder/rods/python/pybind11/rod.hpp>`) pulls in.
*/

#include <welder/lang.hpp>
#include <welder/annotations.hpp>
