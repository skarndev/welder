#pragma once
// Nested-type cases — mirrors tests/test_nested.py / nested_spec.lua (same
// sections, same order).
//
// A type nested inside a welded class resolves like any other class member: the
// OUTER's policy plus the nested type's own exclude/include/only marks decide
// participation — a nested type never carries (or needs) a `weld` of its own.
// Rods with nested placement (pybind11 / nanobind / sol2) register it under the
// outer's binding (`module.Outer.Inner`); LuaBridge3 places it on the module
// table under the dotted name (same Lua access chain either way).
//
// The cases live in namespace `nested`, bound under a `nested` submodule via
// WELDER_TEST_WELDER::weld_namespace so the target package mirrors this file.
//
// #included by bindings.cpp after the welder vocabulary + the active backend.

namespace nested {

// automatic outer: every nested type participates unless marked out.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Robot {
    // nested class -> Robot.Sensor
    struct Sensor {
        double range{1.5};
        double doubled() const { return range * 2.0; }
    };

    // nested scoped enum -> Robot.Mode (values stay qualified: Robot.Mode.active)
    enum class Mode { idle, active, fault };

    // nested unscoped enum -> Robot.Alarm, values ALSO exported onto Robot
    // (Robot.quiet), mirroring C++'s Robot::quiet.
    enum Alarm { quiet, loud };

    // excluded -> bound nowhere
    struct [[=welder::mark::exclude]] Hidden {
        int h{0};
    };

    // excluded for lua only -> Python binds it, Lua does not
    struct [[=welder::mark::exclude(welder::lang::lua)]] Config {
        int level{3};
    };

    // members whose types are the nested types above: the bindability gate
    // passes because the nested-type sweep registers them with Robot itself.
    Sensor sensor{};
    Mode mode{Mode::idle};

    Mode get_mode() const { return mode; }
    void set_mode(Mode m) { mode = m; }
    Alarm alarm_for(Mode m) const {
        return m == Mode::fault ? loud : quiet;
    }
};

// nesting recurses: Machine.Part.Bolt
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Machine {
    struct Part {
        struct Bolt {
            int size{5};
        };
        Bolt bolt{};
    };
    Part part{};
};

// opt_in outer: a nested type binds only when explicitly included — exactly
// like a data member under opt_in.
struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::opt_in
]]
Panel {
    struct [[=welder::mark::include]] Knob {
        int pos{0};
    };
    struct Wiring { // not opted in -> not bound
        int gauge{12};
    };
    [[=welder::mark::include]] int width{10};
};

// a PROTECTED nested type binds when the outer admits protected members
// (policy::weld_protected) — it resolves like any admitted member.
struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::weld_protected
]]
Rig {
    int id{1};

  protected:
    struct Jig {
        int slots{7};
    };
};

// a PRIVATE nested type never binds (and is skipped without error).
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Cabinet {
    int drawers{2};

  private:
    struct Stash {
        int gold{0};
    };
};

} // namespace nested

inline void register_nested(WELDER_TEST_MODULE_T& m) {
    // Whole namespace under a `nested` submodule; each outer type's nested types
    // ride its weld_type (they are not namespace members, so the namespace walk
    // never sees them directly).
    auto sub{WELDER_TEST_SUBMODULE(m, "nested")};
    WELDER_TEST_WELDER::weld_namespace<^^nested>(sub);
}