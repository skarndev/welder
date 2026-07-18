// welder LuaBridge3 (Lua) test module. Reuses the *same* backend-neutral C++ case
// tree as the sol2 / pybind11 / nanobind extensions (tests/common/cpp) — the cases
// are welded for lang::lua as well as lang::py, and reach the backend only through
// the seam macros below. The *same* busted specs (tests/lua/spec/*_spec.lua) assert
// this module too, selected at run time via WELDER_TEST_LUA_MODULE — a cross-backend
// consistency check between the two Lua rods.
//
// Header-only consumption (welder::headers); the LuaBridge3 backend + Lua headers
// come through welder::luabridge. welder is header-only today; a Lua binding TU
// stays header-only regardless (see cmake/WelderLuaBridgeModule.cmake).
#include <cstdint>
#include <string>

#include <welder/vocabulary.hpp>

#include <welder/rods/lua/luabridge/rod.hpp>

#include <LuaBridge/Variant.h> // std::variant in unions.hpp (the blessed path)

// Rod selection for the shared case headers.
#define WELDER_TEST_WELDER ::welder::welder<::welder::rods::luabridge::rod>
// The naming group binds through a styled welder; Lua has no house style, so the
// all-snake_case uniform style stands in (reshaping types + members alike).
#define WELDER_TEST_STYLED_WELDER \
    ::welder::welder<::welder::rods::luabridge::rod, ::welder::naming::snake_case>
#define WELDER_TEST_MODULE_T ::welder::rods::luabridge::rod::module_type
// WELDER_TEST_MULTIPLE_INHERITANCE is deliberately NOT defined: the shared diamond
// case uses *virtual* base classes, and LuaBridge3's base-cast offset is plain
// pointer arithmetic (computeCastOffset) that a virtual base breaks — registering
// one crashes at load. LuaBridge3 does support *non-virtual* multiple inheritance,
// but the shared case is virtual, so the diamond is skipped here (like nanobind's
// single-inheritance gating); the busted inheritance spec skips it when Bottom is
// absent.

// The Lua form of the register_* helpers' submodule seam: a LuaBridge3 submodule is
// a nested namespace, which the rod already knows how to create.
#define WELDER_TEST_SUBMODULE(m, name) \
    ::welder::rods::luabridge::rod::add_submodule((m), (name))

// Chaining seam (chaining.hpp): a hand-written LuaBridge3 registration on the
// class handle weld_type returns — re-open the class from the handle's recorded
// module path + name (the rod's own re-open-by-path model) and add a method the
// LuaBridge3 way. WELDER_TEST_CHAIN_FN_ALIAS is deliberately NOT defined: the
// fluent registrar has no per-function handle, so weld_function is void here.
namespace {
template <class Handle>
void welder_test_chain_extra(Handle& cls) {
    ::luabridge::Namespace ns{::luabridge::getGlobalNamespace(cls.mod.L)};
    for (const auto& seg : cls.mod.path)
        ns = ns.beginNamespace(seg.c_str());
    ns.beginClass<typename Handle::type>(cls.name.c_str())
        .addFunction("doubled",
                     +[](const typename Handle::type* g) { return g->n * 2; })
        .endClass();
}
} // namespace
#define WELDER_TEST_CHAIN_CLASS_EXTRA(cls) welder_test_chain_extra(cls)

// The shared case groups. doc.hpp is omitted: it exercises Python __doc__ (which Lua
// has no runtime home for) and its build_module hooks use module_::attr; the
// backend-specific trust.hpp / caster.hpp are omitted too.
#include "resolution.hpp"
#include "methods.hpp"
#include "inheritance.hpp"
#include "namespace.hpp"
#include "operators.hpp"
#include "enums.hpp"
#include "nested.hpp"
#include "naming.hpp"
#include "chaining.hpp"
#include "overloads.hpp"
#include "retpolicy.hpp"
#include "properties.hpp"
#include "templates.hpp"
#include "unions.hpp"

// The Lua entry point require("welder_test_luabridge") calls. Builds the module (a
// named namespace under _G), fills it from every case group (each under its own
// submodule, mirroring the Python package layout), and returns the table — clearing
// the _G binding so `require` yields the table without a global side effect.
extern "C" int luaopen_welder_test_luabridge(lua_State* L) {
    ::welder::rods::luabridge::rod::module_type m{L, {"welder_test_luabridge"}};
    register_resolution(m);   // <-> resolution.hpp
    register_methods(m);      // <-> methods.hpp
    register_inheritance(m);  // <-> inheritance.hpp
    register_namespace(m);    // <-> namespace.hpp
    register_freestanding(m); // <-> namespace_spec.lua (semi-manual weld_function/variable)
    register_foreign(m);      // <-> namespace_spec.lua (tack-welding an unmarked namespace)
    register_operators(m);    // <-> operators.hpp
    register_enums(m);        // <-> enums.hpp
    register_nested(m);      // <-> nested.hpp
    register_naming(m);       // <-> naming_spec.lua
    register_chaining(m);     // <-> chaining_spec.lua (handles returned by weld_*)
    register_overloads(m);    // <-> overloads_spec.lua (per-overload / per-ctor marks)
    register_retpolicy(m);    // <-> retpolicy_spec.lua (return_policy is structural in Lua)
    register_properties(m); // <-> properties_spec.lua (getter/setter marks)
    register_templates(m);   // <-> templates_spec.lua (alias-welded template instantiations)
    register_unions(m);      // <-> unions_spec.lua (union escape hatches + std::variant)
    lua_getglobal(L, "welder_test_luabridge"); // the populated module table
    lua_pushnil(L);
    lua_setglobal(L, "welder_test_luabridge"); // keep _G clean
    return 1;
}