// The C# bindings generator over the SHARED inheritance cases
// (tests/common/cpp/inheritance.hpp, welded for lang::cs): welded base chains
// (C# base classes over upcast-chained per-level handles), non-welded-base
// flattening, the welded-base-through-a-bridge link, and the virtual diamond
// (extra bases as As<Base>() views).
#include "shared_seam.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(inheritance, "shared_seam.hpp", "welder_test_cs_inheritance")
