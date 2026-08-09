#pragma once
// The C# rod's seam for the SHARED case tree (tests/common/cpp): those headers
// end in a register_<group>(WELDER_TEST_MODULE_T&) helper written against the
// module-seam macros every backend defines for itself. The C# generator drives
// whole namespaces through WELDER_CSHARP_MAIN instead, so the helpers are never
// called here — but they must still compile, hence the macro definitions.
// Include this (not the shared header directly) from both the generator TU and
// as the generated shim's include.
#include <cstdlib>

#include <welder/vocabulary.hpp>
#include <welder/rods/csharp/rod.hpp>
// The shared overridable cases spell the Python-side trampoline hooks
// (rods::python::trampoline_for) — pure reflection, no Python dep.
#include <welder/rods/python/trampoline.hpp>

#define WELDER_TEST_MODULE_T ::welder::rods::csharp::module_writer
#define WELDER_TEST_SUBMODULE(m, name) \
    ::welder::rods::csharp::rod::add_submodule((m), (name))
#define WELDER_TEST_WELDER \
    ::welder::welder<::welder::rods::csharp::rod, ::welder::rods::csharp::dotnet>
// The dotnet style IS this backend's styled welder (naming.hpp's seam).
#define WELDER_TEST_STYLED_WELDER WELDER_TEST_WELDER

// The virtual-diamond MI case: the C# rod represents extra welded bases as
// As<Base>() views, so it participates (like sol2, unlike LuaBridge3).
#define WELDER_TEST_MULTIPLE_INHERITANCE 1

// Neutral stubs for the shared overridable cases' Py* trampoline structs: the
// C# rod GENERATES its directors, so those hand-authored trampolines are inert
// here — their bodies only have to compile (they are never instantiated). The
// stub consumes the forwarded arguments and converts to any return type; it
// aborts if ever actually reached.
namespace wcs_seam {
struct any_result {
    template <class T>
    [[noreturn]] operator T() const {
        std::abort();
    }
};
[[noreturn]] inline any_result override_stub(auto&&... a) {
    ((void)a, ...);
    std::abort();
}
} // namespace wcs_seam
#define WELDER_PY_TRAMPOLINE(T, B) using B::B
#define WELDER_PY_OVERRIDE(name, ...) return ::wcs_seam::override_stub(__VA_ARGS__)
#define WELDER_PY_OVERRIDE_AS(slot, name, ...) \
    return ::wcs_seam::override_stub(__VA_ARGS__)

#include "copying.hpp"
#include "enums.hpp"
#include "inheritance.hpp"
#include "methods.hpp"
#include "namespace.hpp"
#include "naming.hpp"
#include "nested.hpp"
#include "overloads.hpp"
#include "overridable.hpp"
#include "properties.hpp"
#include "resolution.hpp"
#include "templates.hpp"
#include "operators.hpp"
#include "retpolicy.hpp"
