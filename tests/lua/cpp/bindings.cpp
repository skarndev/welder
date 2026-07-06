// welder sol2 (Lua) test module. Reuses the *same* backend-neutral C++ case tree
// as the pybind11 / nanobind extensions (tests/common/cpp) — the cases are welded
// for lang::lua as well as lang::py, and reach the backend only through the seam
// macros below. Only the assertions differ: Lua behaviour (metamethods, tables,
// per-language marks) is checked by the LuaUnit specs in tests/lua/, not pytest.
//
// Header-only consumption (welder::headers); the sol2 backend + Lua headers come
// through welder::sol2. There is no module-form variant: sol2's <luaconf.h> does
// not survive C++20 module dependency scanning, so a Lua binding TU is always
// header-only (see cmake/WelderSol2Module.cmake).
#include <cstdint>
#include <string>

#include <welder/vocabulary.hpp>

#include <sol/sol.hpp>
#include <welder/rods/lua/sol2/rod.hpp>

// Rod selection for the shared case headers. sol2 supports multiple base
// classes, so the multiple-inheritance diamond is enabled (like pybind11).
#define WELDER_TEST_WELDER ::welder::welder<::welder::rods::sol2::rod>
// The naming group binds through a styled welder; Lua has no house style, so the
// all-snake_case uniform style stands in (reshaping types + members alike).
#define WELDER_TEST_STYLED_WELDER \
    ::welder::welder<::welder::rods::sol2::rod, ::welder::naming::snake_case>
#define WELDER_TEST_MODULE_T ::sol::table
#define WELDER_TEST_MULTIPLE_INHERITANCE 1

namespace {
// The Lua form of the register_* helpers' submodule seam: a Lua submodule is just
// a nested table. Mirrors what the sol2 rod's own add_submodule does.
inline ::sol::table welder_test_submodule(::sol::table& m, const char* name) {
    ::sol::table sub{::sol::state_view{m.lua_state()}.create_table()};
    m[name] = sub;
    return sub;
}
} // namespace
#define WELDER_TEST_SUBMODULE(m, name) welder_test_submodule((m), (name))

// The shared case groups. doc.hpp is omitted: it exercises Python __doc__ (which
// Lua has no runtime home for) and its build_module hooks use module_::attr; the
// backend-specific trust.hpp / caster.hpp are omitted too.
#include "resolution.hpp"
#include "methods.hpp"
#include "inheritance.hpp"
#include "namespace.hpp"
#include "operators.hpp"
#include "enums.hpp"
#include "naming.hpp"

// The Lua entry point require("welder_test_sol2") calls. Builds the module table,
// fills it from every case group (each under its own submodule table, mirroring
// the Python package layout), and returns it.
extern "C" int luaopen_welder_test_sol2(lua_State* L) {
    ::sol::state_view lua(L);
    ::sol::table m{lua.create_table()};
    register_resolution(m);  // <-> resolution.hpp
    register_methods(m);     // <-> methods.hpp
    register_inheritance(m); // <-> inheritance.hpp
    register_namespace(m);   // <-> namespace.hpp
    register_operators(m);   // <-> operators.hpp
    register_enums(m);       // <-> enums.hpp
    register_naming(m);      // <-> naming_spec.lua
    return ::sol::stack::push(L, m);
}
