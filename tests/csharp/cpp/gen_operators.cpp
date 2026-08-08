// The C# bindings generator over the SHARED operator cases
// (tests/common/cpp/operators.hpp, welded for lang::cs) — arithmetic, unary,
// comparisons with pairing, free/reflected operators, subscript, spaceship
// synthesis and the ostream stringifier, the same surface the Python/Lua
// specs assert.
#include "shared_seam.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(operators, "shared_seam.hpp", "welder_test_cs_operators")
