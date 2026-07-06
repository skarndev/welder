// Negative-compile: welder must reject binding a type whose data member is a
// program-defined class that sol2 can only convert once registered — and which is
// NOT welded. The bindability gate turns that into a hard compile error at the
// bind<> instantiation (there is no meaningful Lua value for the member, and any
// stub would reference an unregistered type). Built by the negcompile.sol2_unwelded
// CTest, which is marked WILL_FAIL.
#include <welder/vocabulary.hpp>

#include <sol/sol.hpp>
#include <welder/rods/lua/sol2/rod.hpp>

// Not welded -> sol2 has no usertype for it, and welder was not told to trust it.
struct Unwelded {
    int x{0};
};

struct [[=welder::weld(welder::lang::lua)]]
HasUnwelded {
    Unwelded member; // no native sol2 conversion, not welded -> gate rejects
};

int main() {
    sol::state lua;
    sol::table m{lua.create_table()};
    welder::welder<welder::rods::sol2::rod>::weld_type<HasUnwelded>(m); // <-- hard compile error here
}
