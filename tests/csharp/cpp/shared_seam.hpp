#pragma once
// The C# rod's seam for the SHARED case tree (tests/common/cpp): those headers
// end in a register_<group>(WELDER_TEST_MODULE_T&) helper written against the
// module-seam macros every backend defines for itself. The C# generator drives
// whole namespaces through WELDER_CSHARP_MAIN instead, so the helpers are never
// called here — but they must still compile, hence the macro definitions.
// Include this (not the shared header directly) from both the generator TU and
// as the generated shim's include.
#include <welder/vocabulary.hpp>
#include <welder/rods/csharp/rod.hpp>

#define WELDER_TEST_MODULE_T ::welder::rods::csharp::module_writer
#define WELDER_TEST_SUBMODULE(m, name) \
    ::welder::rods::csharp::rod::add_submodule((m), (name))
#define WELDER_TEST_WELDER \
    ::welder::welder<::welder::rods::csharp::rod, ::welder::rods::csharp::dotnet>

// The virtual-diamond MI case: the C# rod represents extra welded bases as
// As<Base>() views, so it participates (like sol2, unlike LuaBridge3).
#define WELDER_TEST_MULTIPLE_INHERITANCE 1

#include "inheritance.hpp"
#include "operators.hpp"
#include "retpolicy.hpp"
