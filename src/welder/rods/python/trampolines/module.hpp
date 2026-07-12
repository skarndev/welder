#pragma once
/** @file
    Full-automation entry point for the trampoline-generator rod: the
    `WELDER_TRAMPOLINES_MAIN` generator-`main()` macro.

    Include this (instead of `rod.hpp`) in a trampoline-generator TU so the whole
    executable is one macro line; `welder_generate_trampolines()` (CMake) builds and
    runs it into `<name>.trampolines.hpp`.
*/
#include <fstream>  // write the header to an output-path argument
#include <iostream> // default to stdout

#include <welder/rods/python/trampolines/rod.hpp>

/** @def WELDER_TRAMPOLINES_MAIN
    Define a `main()` that emits the trampoline header for namespace @a ns.

    Writes to the file named by the first command-line argument, or to stdout when none
    is given. The build-time analogue of a backend entry point: a generator executable
    links this and `welder_generate_trampolines` runs it with the output path to produce
    the trampoline header consumed by the real binding TU.
    @param ns the top-level namespace / module name token. */
#define WELDER_TRAMPOLINES_MAIN(ns)                                               \
    int main(int argc, char** argv) {                                            \
        if (argc > 1) {                                                          \
            ::std::ofstream welder_out_{argv[1]};                               \
            ::welder::rods::trampolines::rod::generate<^^ns>(welder_out_);       \
        } else {                                                                 \
            ::welder::rods::trampolines::rod::generate<^^ns>(::std::cout);       \
        }                                                                        \
        return 0;                                                                \
    }
