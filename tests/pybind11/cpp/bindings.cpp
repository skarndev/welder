// Test extension exercising every member-resolution case. Built twice — once
// consuming `import welder;` and once consuming welder header-only — from this
// single source, selected by WELDER_TEST_HEADER_ONLY. The module name comes from
// WELDER_TEST_MODNAME so each build produces a distinct importable module.
//
// The cases are split into one header per Python test file, in the same order and
// with section banners that line up with the .py side:
//   resolution.hpp  <-> tests/test_resolution.py
//   methods.hpp     <-> tests/test_methods.py
//   inheritance.hpp <-> tests/test_inheritance.py
//   namespace.hpp   <-> tests/test_namespace.py
//   doc.hpp         <-> tests/test_doc.py
#include <cstdint>
#include <string>

#ifdef WELDER_TEST_HEADER_ONLY
#  include <welder/welder.hpp>
#else
import welder;
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/backends/pybind11.hpp>

// Case groups. These must come after the vocabulary + backend above: they use
// both and deliberately do not re-include them (in the module build the
// vocabulary arrives via `import welder;`, which must not be textually included).
#include "resolution.hpp"
#include "methods.hpp"
#include "inheritance.hpp"
#include "namespace.hpp"
#include "doc.hpp"
#include "operators.hpp"
#include "trust.hpp"

#ifndef WELDER_TEST_MODNAME
#  define WELDER_TEST_MODNAME welder_test_pybind11
#endif

PYBIND11_MODULE(WELDER_TEST_MODNAME, m) {
    m.doc() = "welder pybind11 test bindings";
    register_resolution(m);  // <-> test_resolution.py
    register_methods(m);     // <-> test_methods.py
    register_inheritance(m); // <-> test_inheritance.py
    register_namespace(m);   // <-> test_namespace.py
    register_doc(m);         // <-> test_doc.py
    register_operators(m);   // <-> test_operators.py
    register_trust(m);       // <-> test_trust.py
}
