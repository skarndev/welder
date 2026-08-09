// The C# bindings generator over the SHARED naming/styling cases
// (tests/common/cpp/naming.hpp, welded for lang::cs): the dotnet PascalCase
// style over camelCase C++ names, and per-language weld_as scoping (the
// py/lua-scoped overrides do NOT cover cs, so the style applies).
#include "shared_seam.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(styling, "shared_seam.hpp", "welder_test_cs_naming")
