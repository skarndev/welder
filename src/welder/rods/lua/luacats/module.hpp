#pragma once
/** @file
    Full-automation entry point for the LuaCATS stub rod: the
    `WELDER_LUACATS_MAIN` generator-`main()` macro.

    Include this (instead of `rod.hpp`) in a stub-generator TU so the whole
    executable is one macro line; `welder_luacats_generate_stub()` (CMake) builds
    and runs it into `<name>.lua`.
*/
#include <fstream>  // write the stub to an output-path argument
#include <iostream> // default to stdout

#include <welder/rods/lua/luacats/rod.hpp>

/** @def WELDER_LUACATS_MAIN
    Define a `main()` that emits the LuaCATS stub for namespace @a ns.

    Writes to the file named by the first command-line argument, or to stdout when
    none is given. The build-time analogue of a backend entry point: a generator
    executable links this and `welder_luacats_generate_stub` runs it with the
    output path to produce `<ns>.lua`.
    @param ns the top-level namespace / module name token. */
#define WELDER_LUACATS_MAIN(ns)                                                   \
    int main(int argc, char** argv) {                                             \
        if (argc > 1) {                                                           \
            ::std::ofstream welder_out_{argv[1]};                                 \
            ::welder::rods::luacats::rod::generate<^^ns>(welder_out_);             \
        } else {                                                                  \
            ::welder::rods::luacats::rod::generate<^^ns>(::std::cout);             \
        }                                                                         \
        return 0;                                                                 \
    }
