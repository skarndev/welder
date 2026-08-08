// The C# bindings generator over the SHARED retpolicy cases
// (tests/common/cpp/retpolicy.hpp, welded for lang::cs) — the cross-backend
// consistency check: the same Owner/Inner pair the Python/Lua specs assert.
#include "shared_seam.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(retpolicy, "shared_seam.hpp", "welder_test_cs_retpolicy")
