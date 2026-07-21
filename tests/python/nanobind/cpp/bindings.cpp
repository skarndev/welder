// nanobind test extension exercising every member-resolution case. welder is
// consumed header-only (via <welder/vocabulary.hpp>). The module name comes from
// WELDER_TEST_MODNAME so the build produces a distinctly importable module.
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

#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h> // std::string members / return values
#include <nanobind/stl/vector.h> // std::vector<Handmade2> in trust.hpp; stl.hpp
#include <nanobind/stl/variant.h> // std::variant in unions.hpp (the blessed path)
#include <nanobind/stl/map.h>      // std::map in stl.hpp
#include <nanobind/stl/optional.h> // std::optional in stl.hpp
#include <nanobind/stl/pair.h>     // std::pair in stl.hpp
#include <welder/rods/python/nanobind/rod.hpp>
#include <welder/rods/python/nanobind/trampoline.hpp> // virtual-override support
#include <welder/rods/python/naming.hpp> // the PEP 8 style for the naming group

// Rod selection for the shared case headers (tests/common/cpp). nanobind binds
// only single inheritance, so WELDER_TEST_MULTIPLE_INHERITANCE is deliberately left
// undefined — the diamond case is skipped here and its Python spec skips too.
#define WELDER_TEST_WELDER ::welder::welder<::welder::rods::nanobind::rod<>>
// The naming group binds through a styled welder (PEP 8 for Python).
#define WELDER_TEST_STYLED_WELDER \
    ::welder::welder<::welder::rods::nanobind::rod<>, ::welder::rods::python::pep8>
// The doc group re-welds one function through numpy- and sphinx-styled rods (the
// docstring dialect is the rod's DocStyle template parameter).
#define WELDER_TEST_NUMPY_WELDER \
    ::welder::welder<::welder::rods::nanobind::rod<::welder::rods::python::numpy_style>>
#define WELDER_TEST_SPHINX_WELDER \
    ::welder::welder<::welder::rods::nanobind::rod<::welder::rods::python::sphinx_style>>
#define WELDER_TEST_MODULE_T ::nanobind::module_
// The backend's generic-object type (copying.hpp's greedy-constructor case:
// a ctor accepting ANY Python object serves `T(other)`, and the copy protocol
// must still copy faithfully — it never routes through constructor overloads).
#define WELDER_TEST_PYOBJECT ::nanobind::object
// The one module-handle op the shared register_* helpers need; the Lua backend,
// whose handle is a sol::table with no def_submodule, defines it differently.
#define WELDER_TEST_SUBMODULE(m, name) (m).def_submodule(name)
// The opaque-container declaration for opaque.hpp (WELDER_OPAQUE is this rod's
// NB_MAKE_OPAQUE). Must expand at namespace scope, before the module — it disables
// the stl/*.h catch-all caster for exactly those container types, so they bind by
// reference while the by-value stl.hpp group's OTHER containers keep converting.
#define WELDER_TEST_MAKE_OPAQUE(...) WELDER_OPAQUE(__VA_ARGS__)
// Chaining seams (chaining.hpp): hand-written nanobind registrations on the
// handles weld_type / weld_function return.
#define WELDER_TEST_CHAIN_CLASS_EXTRA(cls) \
    (cls).def("doubled", [](const chaining::Gadget& g) { return g.n * 2; })
#define WELDER_TEST_CHAIN_FN_ALIAS(sub, fn) (sub).attr("twice_alias") = (fn)
// Member/free function-template instantiations appended under the SAME names as
// welder-bound non-template overload groups — nanobind merges them into one
// overloaded function (exact matches win across all overloads, so order is moot).
#define WELDER_TEST_CHAIN_TPL_OVERLOAD(sub, cls)          \
    (cls).def("mix", &chaining::Mixer::mix<double>);      \
    (sub).def("blend", &chaining_tpl_fns::blend<double>)

// Case groups. These must come after the vocabulary + backend above: they use
// both and deliberately do not re-include them. The backend-neutral groups resolve
// from tests/common/cpp (on the include path); the two backend-specific ones
// (trust, caster) live alongside this file.
#include "resolution.hpp"
#include "methods.hpp"
#include "inheritance.hpp"
#include "overridable.hpp"
#include "gen_trampolines.hpp"                // welded virtual types (trampolines generated)
#include "gen_trampolines.trampolines.hpp"   // generated by welder::rods::trampolines
#include "namespace.hpp"
#include "doc.hpp"
#include "operators.hpp"
#include "trust.hpp"
#include "caster.hpp"
#include "enums.hpp"
#include "nested.hpp"
#include "naming.hpp"
#include "chaining.hpp"
#include "overloads.hpp"
#include "retpolicy.hpp"
#include "properties.hpp"
#include "templates.hpp"
#include "unions.hpp"
#include "copying.hpp"
#include "stl.hpp"
#include "opaque.hpp"

#ifndef WELDER_TEST_MODNAME
#  define WELDER_TEST_MODNAME welder_test_nanobind
#endif

NB_MODULE(WELDER_TEST_MODNAME, m) {
    m.attr("__doc__") = "welder nanobind test bindings";
    register_resolution(m);  // <-> test_resolution.py
    register_methods(m);     // <-> test_methods.py
    register_inheritance(m); // <-> test_inheritance.py
    register_overridable(m); // <-> test_trampoline.py
    register_gen_trampolines(m); // <-> test_gen_trampolines.py (generated trampolines)
    register_namespace(m);   // <-> test_namespace.py
    register_freestanding(m); // <-> test_namespace.py (semi-manual weld_function/variable)
    register_foreign(m);     // <-> test_namespace.py (tack-welding an unmarked namespace)
    register_doc(m);         // <-> test_doc.py
    register_doc_styles(m);  // <-> test_doc.py (numpy/sphinx docstring dialects)
    register_operators(m);   // <-> test_operators.py
    register_trust(m);       // <-> test_trust.py
    register_caster(m);      // <-> test_caster.py
    register_enums(m);       // <-> test_enums.py
    register_nested(m);      // <-> test_nested.py (class-nested types)
    register_naming(m);      // <-> test_naming.py
    register_chaining(m);    // <-> test_chaining.py (handles returned by weld_*)
    register_overloads(m);   // <-> test_overloads.py (per-overload / per-ctor marks)
    register_retpolicy(m);   // <-> test_retpolicy.py (return_policy + keep_alive)
    register_properties(m); // <-> test_properties.py (getter/setter marks)
    register_templates(m);   // <-> test_templates.py (alias-welded template instantiations)
    register_unions(m);      // <-> test_unions.py (union escape hatches + std::variant)
    register_copying(m);     // <-> test_copying.py (__copy__/__deepcopy__ via the copy ctor)
    register_stl(m);         // <-> test_stl.py (STL-container conversions; typed in test_types.mypy-testing)
    register_opaque(m);      // <-> test_opaque.py (opaque, reference-semantic containers)
}
