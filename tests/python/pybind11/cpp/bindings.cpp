// Test extension exercising every member-resolution case. Built twice — once
// consuming `import welder;` and once consuming welder header-only — from this
// single source, selected by WELDER_TEST_HEADER_ONLY. The module name comes from
// WELDER_TEST_MODNAME so each build produces a distinct importable module.
//
// The cases are split into one header per Python test file, in the same order and
// with section banners that line up with the .py side. Each header wraps its cases
// in a namespace and binds them under a same-named submodule (via bind_namespace /
// build_module), so the Python package structure mirrors this file layout:
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
#  include <welder/vocabulary.hpp>
#else
import welder;
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/rod.hpp>
#include <welder/rods/python/naming.hpp> // the PEP 8 style for the naming group

// Rod selection for the shared case headers (tests/common/cpp): they are
// backend-neutral and reach the backend only through these two names, so the same
// sources drive both the pybind11 and nanobind extensions. pybind11 supports
// multiple inheritance, so the diamond case is enabled here.
#define WELDER_TEST_WELDER ::welder::welder<::welder::rods::pybind11::rod>
// The naming group binds through a styled welder (PEP 8 for Python).
#define WELDER_TEST_STYLED_WELDER \
    ::welder::welder<::welder::rods::pybind11::rod, ::welder::rods::python::pep8>
#define WELDER_TEST_MODULE_T ::pybind11::module_
#define WELDER_TEST_MULTIPLE_INHERITANCE 1
// The one module-handle op the shared register_* helpers need; the Lua backend,
// whose handle is a sol::table with no def_submodule, defines it differently.
#define WELDER_TEST_SUBMODULE(m, name) (m).def_submodule(name)

// Case groups. These must come after the vocabulary + backend above: they use
// both and deliberately do not re-include them (in the module build the
// vocabulary arrives via `import welder;`, which must not be textually included).
// The backend-neutral groups live in tests/common/cpp (on the include path); the
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
#include "naming.hpp"

#ifndef WELDER_TEST_MODNAME
#  define WELDER_TEST_MODNAME welder_test_pybind11
#endif

PYBIND11_MODULE(WELDER_TEST_MODNAME, m) {
    m.doc() = "welder pybind11 test bindings";
    register_resolution(m);  // <-> test_resolution.py
    register_methods(m);     // <-> test_methods.py
    register_inheritance(m); // <-> test_inheritance.py
    register_namespace(m);   // <-> test_namespace.py
    register_freestanding(m); // <-> test_namespace.py (semi-manual weld_function/variable)
    register_foreign(m);     // <-> test_namespace.py (tack-welding an unmarked namespace)
    register_doc(m);         // <-> test_doc.py
    register_operators(m);   // <-> test_operators.py
    register_trust(m);       // <-> test_trust.py
    register_caster(m);      // <-> test_caster.py
    register_enums(m);       // <-> test_enums.py
    register_naming(m);      // <-> test_naming.py
}
