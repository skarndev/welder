// nanobind test extension exercising every member-resolution case. Built twice —
// once consuming `import welder;` and once consuming welder header-only — from this
// single source, selected by WELDER_TEST_HEADER_ONLY. The module name comes from
// WELDER_TEST_MODNAME so each build produces a distinct importable module.
//
// This mirrors tests/pybind11/cpp/bindings.cpp against the nanobind backend. The
// backend-neutral case groups are the *shared* headers under tests/common/cpp (on
// the include path); only trust.hpp and caster.hpp — which hand-register types /
// define a type_caster — are nanobind-specific and live alongside this file. Each
// group binds under a same-named submodule, so the Python package mirrors this file
// exactly as the pybind11 build does, and the shared specs (tests/test_*.py) run
// against both:
//   resolution.hpp  <-> tests/test_resolution.py   (submodule `resolution`)
//   methods.hpp     <-> tests/test_methods.py       (submodule `methods`)
//   inheritance.hpp <-> tests/test_inheritance.py   (submodule `inheritance`)
//   namespace.hpp   <-> tests/test_namespace.py     (submodule `catalog`)
//   doc.hpp         <-> tests/test_doc.py           (submodule `documented`)
//   operators.hpp   <-> tests/test_operators.py     (submodule `operators`)
//   trust.hpp       <-> tests/test_trust.py         (submodule `trust`)
//   caster.hpp      <-> tests/test_caster.py        (submodule `caster`)
//   enums.hpp       <-> tests/test_enums.py         (submodule `enums`)
#include <cstdint>
#include <string>

#ifdef WELDER_TEST_HEADER_ONLY
#  include <welder/welder.hpp>
#else
import welder;
#endif

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h> // std::string members / return values
#include <nanobind/stl/vector.h> // std::vector<Handmade2> in trust.hpp
#include <welder/backends/python/nanobind/backend.hpp>

// Backend selection for the shared case headers (tests/common/cpp). nanobind binds
// only single inheritance, so WELDER_TEST_MULTIPLE_INHERITANCE is deliberately left
// undefined — the diamond case is skipped here and its Python spec skips too.
#define WELDER_TEST_BE welder::nanobind
#define WELDER_TEST_MODULE_T ::nanobind::module_
// The one module-handle op the shared register_* helpers need; the Lua backend,
// whose handle is a sol::table with no def_submodule, defines it differently.
#define WELDER_TEST_SUBMODULE(m, name) (m).def_submodule(name)

// Case groups. These must come after the vocabulary + backend above: they use
// both and deliberately do not re-include them (in the module build the vocabulary
// arrives via `import welder;`, which must not be textually included). The
// backend-neutral groups resolve from tests/common/cpp (on the include path); the
// two backend-specific ones (trust, caster) live alongside this file.
#include "resolution.hpp"
#include "methods.hpp"
#include "inheritance.hpp"
#include "namespace.hpp"
#include "doc.hpp"
#include "operators.hpp"
#include "trust.hpp"
#include "caster.hpp"
#include "enums.hpp"

#ifndef WELDER_TEST_MODNAME
#  define WELDER_TEST_MODNAME welder_test_nanobind
#endif

NB_MODULE(WELDER_TEST_MODNAME, m) {
    m.attr("__doc__") = "welder nanobind test bindings";
    register_resolution(m);  // <-> test_resolution.py
    register_methods(m);     // <-> test_methods.py
    register_inheritance(m); // <-> test_inheritance.py
    register_namespace(m);   // <-> test_namespace.py
    register_doc(m);         // <-> test_doc.py
    register_operators(m);   // <-> test_operators.py
    register_trust(m);       // <-> test_trust.py
    register_caster(m);      // <-> test_caster.py
    register_enums(m);       // <-> test_enums.py
}
