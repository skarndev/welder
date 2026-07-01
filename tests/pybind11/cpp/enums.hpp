#pragma once
// Enum cases — mirrors tests/test_enums.py (same sections, same order).
//
// A welded enum binds as a py::enum_: each enumerator resolves like a data member,
// honoring the enum's policy and per-enumerator exclude/include marks. NOTE the
// grammar — an enumerator's annotation goes *after* its name (`South [[=...]]`),
// unlike a struct member's (which precedes it). A scoped enum is reached as
// E.Value; an unscoped enum also exports its values into the enclosing scope,
// mirroring C++.
//
// #included by bindings.cpp after the welder vocabulary + pybind11 backend.

// scoped enum, automatic policy: all enumerators bound except the excluded one.
enum class
[[=welder::weld(welder::lang::py)]]
Direction {
    North,
    East,
    South [[=welder::mark::exclude]], // excluded -> not bound (value 2 still skipped)
    West                              // keeps its C++ value (3)
};

// unscoped enum: its values are also exported unqualified into the module scope.
enum
[[=welder::weld(welder::lang::py)]]
Signal {
    Green,
    Yellow,
    Red
};

// scoped enum, opt_in policy: only explicitly included enumerators bind.
enum class
[[
  =welder::weld(welder::lang::py),
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
[[=welder::weld(welder::lang::py)]]
Compass {
    Direction facing;
};

inline void register_enums(pybind11::module_& m) {
    welder::pybind11::bind<Direction>(m); // bind enums before the struct using them
    welder::pybind11::bind<Signal>(m);
    welder::pybind11::bind<Level>(m);
    welder::pybind11::bind<Compass>(m);
}
