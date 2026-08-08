// The C#/.NET bindings generator over the dedicated case slice: emits shim.cpp
// (argv[1]) + Bindings.cs (argv[2]) for the csharp_cases namespace. Built and run
// by welder_csharp_generate_bindings() (tests/csharp/CMakeLists.txt).
#include <welder/vocabulary.hpp>
#include "cases.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(csharp_cases, "cases.hpp", "welder_test_csharp_native")
