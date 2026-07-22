#pragma once
/** @file
    Full-automation entry point for the opaque-container generator rod: the
    `WELDER_OPAQUE_CONTAINERS_MAIN` generator-`main()` macro.

    Include this (instead of `rod.hpp`) in a generator TU so the whole executable is
    one macro line; `welder_generate_opaque_containers()` (CMake) builds and runs it
    into `<name>.opaque.hpp`.
*/
#include <fstream>  // write the header to an output-path argument
#include <iostream> // default to stdout

#include <welder/rods/python/opaque_containers/rod.hpp>

/** @def WELDER_OPAQUE_CONTAINERS_MAIN_STYLED
    Like @ref WELDER_OPAQUE_CONTAINERS_MAIN, but with a custom name @a style — a
    `welder::naming` style that may carry a `transform_opaque_container` hook to name the
    opaque wrappers (see `document.hpp` / the Containers guide). The default macro passes
    `welder::naming::none` (the collision-free derived names).
    @param ns    the top-level namespace / module name token.
    @param style a name-style type (its `transform_opaque_container`, if any, names the
                 wrappers). */
#define WELDER_OPAQUE_CONTAINERS_MAIN_STYLED(ns, style)                                 \
    int main(int argc, char** argv) {                                                   \
        if (argc > 1) {                                                                 \
            ::std::ofstream welder_out_{argv[1]};                                       \
            ::welder::rods::opaque_containers::rod::generate<^^ns, style>(welder_out_); \
        } else {                                                                        \
            ::welder::rods::opaque_containers::rod::generate<^^ns, style>(::std::cout); \
        }                                                                               \
        return 0;                                                                       \
    }

/** @def WELDER_OPAQUE_CONTAINERS_MAIN
    Define a `main()` that emits the opaque-container header for namespace @a ns.

    Writes to the file named by the first command-line argument, or to stdout when
    none is given — the build-time analogue of a backend entry point:
    `welder_generate_opaque_containers` runs the generator with the output path. Uses the
    default (collision-free derived) names; for custom naming use
    @ref WELDER_OPAQUE_CONTAINERS_MAIN_STYLED.
    @param ns the top-level namespace / module name token. */
#define WELDER_OPAQUE_CONTAINERS_MAIN(ns)                                               \
    WELDER_OPAQUE_CONTAINERS_MAIN_STYLED(ns, ::welder::naming::none)
