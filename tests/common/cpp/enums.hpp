#pragma once
// Enum cases — mirrors tests/test_enums.py (same sections, same order).
//
// A welded enum binds as a Python enum.IntEnum: each enumerator resolves like a data member,
// honoring the enum's policy and per-enumerator exclude/include marks. NOTE the
// grammar — an enumerator's annotation goes *after* its name (`South [[=...]]`),
// unlike a struct member's (which precedes it). A scoped enum is reached as
// E.Value; an unscoped enum also exports its values into the enclosing scope,
// mirroring C++.
//
// The cases live in namespace `enums`, bound under an `enums` submodule via
// WELDER_TEST_BE::bind_namespace so the Python package mirrors this file.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.

namespace enums {

// scoped enum, automatic policy: all enumerators bound except the excluded one.
enum class
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Direction {
    North,
    East,
    South [[=welder::mark::exclude]], // excluded -> not bound (value 2 still skipped)
    West                              // keeps its C++ value (3)
};

// unscoped enum: its values are also exported unqualified into the module scope.
enum
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Signal {
    Green,
    Yellow,
    Red
};

// scoped enum, opt_in policy: only explicitly included enumerators bind.
enum class
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::opt_in
]]
Level {
    Debug [[=welder::mark::include]],
    Info [[=welder::mark::include]],
    Trace // not opted in -> not bound
};

// an enum used as a struct member: welder's gate passes because Direction is
// welded, and register_enums binds the enum before the struct that uses it.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Compass {
    Direction facing;
};

} // namespace enums

inline void register_enums(WELDER_TEST_MODULE_T& m) {
    // Whole namespace under an `enums` submodule. bind_namespace visits members in
    // declaration order, so each enum is bound before Compass, which uses one.
    auto sub{WELDER_TEST_SUBMODULE(m, "enums")};
    WELDER_TEST_BE::bind_namespace<^^enums>(sub);
}
