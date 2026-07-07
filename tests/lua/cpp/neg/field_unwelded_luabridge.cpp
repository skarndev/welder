// Negative-compile (LuaBridge3): welder must reject binding a type whose data
// member is a program-defined class that LuaBridge3 can only convert once
// registered — and which is NOT welded. The bindability gate turns that into a hard
// compile error at the weld_type<> instantiation. Built by the
// negcompile.luabridge_unwelded CTest, which is marked WILL_FAIL. Mirrors the sol2
// neg case (cpp/neg/field_unwelded.cpp).
#include <welder/vocabulary.hpp>

#include <welder/rods/lua/luabridge/rod.hpp>

// Not welded -> LuaBridge3 has no class registration for it, and welder was not told
// to trust it.
struct Unwelded {
    int x{0};
};

struct [[=welder::weld(welder::lang::lua)]]
HasUnwelded {
    Unwelded member; // no native LuaBridge3 conversion, not welded -> gate rejects
};

int main() {
    // The gate fires at the weld_type<> instantiation below, before anything runs;
    // a null lua_State suffices for the (never-reached) call.
    ::welder::rods::luabridge::rod::module_type m{nullptr, {"neg"}};
    welder::welder<welder::rods::luabridge::rod>::weld_type<HasUnwelded>(m); // <-- hard compile error
}