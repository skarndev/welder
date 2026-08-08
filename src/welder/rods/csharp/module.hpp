#pragma once
/** @file
    Full-automation entry point for the C#/.NET backend: the `WELDER_CSHARP_MAIN`
    generator-`main()` macro.

    Include this (instead of `rod.hpp`) in a generator TU so the whole executable is
    one macro line; `welder_csharp_generate_bindings()` (CMake) builds and runs it to
    produce `shim.cpp` + `Bindings.cs`, then compiles the shim into a shared library
    and hands the `.cs` to `dotnet`.
*/
#include <fstream>
#include <iostream>

#include <welder/rods/csharp/rod.hpp>

/** @def WELDER_CSHARP_MAIN
    Define a `main()` that emits the C#/.NET bindings for namespace @a ns.

    Writes the native shim to `argv[1]` and the C# wrapper to `argv[2]` (falling back
    to `shim.cpp` / `Bindings.cs`). The build-time analogue of a backend entry point.
    @param ns     the top-level namespace / module token.
    @param header the header the emitted shim `#include`s to see the welded types.
    @param lib    the P/Invoke library name (the shared-lib base name, e.g.
                  `"mymod_native"` → `libmymod_native.{so,dylib}` / `mymod_native.dll`). */
#define WELDER_CSHARP_MAIN(ns, header, lib)                                    \
    int main(int argc, char** argv) {                                          \
        ::welder::rods::csharp::options welder_opts_{};                        \
        welder_opts_.library = (lib);                                          \
        welder_opts_.shim_include = (header);                                  \
        ::std::ofstream welder_shim_{argc > 1 ? argv[1] : "shim.cpp"};         \
        ::std::ofstream welder_cs_{argc > 2 ? argv[2] : "Bindings.cs"};        \
        ::welder::rods::csharp::rod::generate<^^ns>(welder_shim_, welder_cs_,  \
                                                    welder_opts_);             \
        return 0;                                                              \
    }
