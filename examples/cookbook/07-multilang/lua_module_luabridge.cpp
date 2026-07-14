// Cookbook 07 — the Lua side again, via the LuaBridge3 rod. lang::lua is one
// language served by two frameworks: this TU and lua_module.cpp (sol2) bind the
// SAME header with the SAME style, differing only in the rod selector — and the
// same check.lua asserts both modules. (This is also the Lua runtime the Windows
// CI uses: ucrt64 ships Lua 5.5, which LuaBridge3 supports and sol2 does not.)
#include <functional>
#include <string>

#include <welder/vocabulary.hpp>

#include <welder/rods/lua/luabridge/module.hpp>

// Where a framework lacks a native conversion, its TU supplies the framework's
// own idiom — the shared header stays neutral. sol2 converts std::function
// arguments out of the box; LuaBridge3 does not, so teach it with a Stack
// specialization (the same extension point its own Vector.h/Map.h use): a Lua
// function argument becomes a std::function wrapping a registry-anchored LuaRef.
namespace luabridge {

template <>
struct Stack<std::function<void(const std::string&)>> {
    using Type = std::function<void(const std::string&)>;

    [[nodiscard]] static Result push(lua_State*, const Type&) {
        // Only the Lua -> C++ direction is needed (an argument type).
        return makeErrorCode(ErrorCode::InvalidTypeCast);
    }

    [[nodiscard]] static TypeResult<Type> get(lua_State* L, int index) {
        if (!lua_isfunction(L, index))
            return makeErrorCode(ErrorCode::InvalidTypeCast);
        LuaRef ref{LuaRef::fromStack(L, index)};
        return Type{[ref](const std::string& line) { ref(line); }};
    }

    [[nodiscard]] static bool isInstance(lua_State* L, int index) {
        return lua_isfunction(L, index) == 1;
    }
};

} // namespace luabridge

#include "journal.hpp"

WELDER_MODULE(journal, luabridge,
              welder::welder<welder::rods::luabridge::rod,
                             welder::naming::snake_case>) {}