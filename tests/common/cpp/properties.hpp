#pragma once
// Method-backed property cases (getter/setter marks) — mirrors
// tests/python/test_properties.py and tests/lua/spec/properties_spec.lua
// (same sections, same order).
//
// Covered here: the two accessor spellings (overload-style radius()/radius(v),
// prefix-style get_x/set_x in every convention), name derivation (camelCase
// preserved under naming::none; is_ predicates NOT stripped), mixed-convention
// pairing (the getter's spelling is authoritative), explicit names, per-language
// scoping and per-language read-only-ness, the opt_in implication, flattened
// non-welded bases, protected accessors under weld_protected, and a fluent
// (value-returning) setter whose return every rod discards.
//
// #included by bindings.cpp *after* the welder vocabulary and the active
// backend are in scope; this header deliberately does not include them itself.
#include <string>
#include <utility>

// The cases live in namespace `properties`, bound under a `properties`
// submodule via WELDER_TEST_WELDER::weld_namespace so the target-language
// package mirrors this file.
namespace properties {

// --- overload-style accessors: radius() / radius(double) --------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Circle {
    Circle() = default;

    // The overload pair shares one C++ name; arity + the marks split the roles.
    [[=welder::getter]] [[=welder::doc("The circle's radius.")]]
    double radius() const { return radius_; }
    [[=welder::setter]]
    void radius(double r) { radius_ = r; }

    // Prefix style, computed, no setter -> read-only property "area".
    [[=welder::getter]]
    double get_area() const { return 3.141592653589793 * radius_ * radius_; }

    // An ordinary method stays a method (the control).
    double diameter() const { return 2.0 * radius_; }

  private:
    double radius_{1.0};
};

// --- name derivation across spelling conventions -----------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Vehicle {
    Vehicle() = default;

    // camelCase accessors -> property "maxSpeed" (the source convention is
    // preserved under the default naming::none style).
    [[=welder::getter]] int getMaxSpeed() const { return max_speed_; }
    [[=welder::setter]] void setMaxSpeed(int v) { max_speed_ = v; }

    // MIXED conventions pair on the case-normalized words; the getter's
    // spelling is authoritative -> property "scale".
    [[=welder::getter]] double get_scale() const { return scale_; }
    [[=welder::setter]] void SetScale(double v) { scale_ = v; }

    // A predicate is NOT stripped -> read-only property "is_ready".
    [[=welder::getter]] bool is_ready() const { return max_speed_ > 0; }

  private:
    int max_speed_{60};
    double scale_{1.0};
};

// --- explicit names + per-language scoping -----------------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Gauge {
    Gauge() = default;

    // Explicit names pair regardless of the identifiers -> property "level".
    [[=welder::getter("level")]] double raw() const { return level_; }
    [[=welder::setter("level")]] void assign(double v) { level_ = v; }

    // A property for Python only; for Lua the marks don't cover the language,
    // so both functions bind as ordinary methods get_mode/set_mode.
    [[=welder::getter(welder::lang::py)]] int get_mode() const { return mode_; }
    [[=welder::setter(welder::lang::py)]] void set_mode(int m) { mode_ = m; }

    // Read/write in Python, READ-ONLY in Lua: the setter is excluded there.
    [[=welder::getter]] int get_limit() const { return limit_; }
    [[=welder::setter]] [[=welder::mark::exclude(welder::lang::lua)]]
    void set_limit(int v) { limit_ = v; }

  private:
    double level_{0.5};
    int mode_{1};
    int limit_{100};
};

// --- opt_in: an accessor mark implies the opt-in ------------------------------

struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::opt_in
]]
Padlock {
    Padlock() = default;

    // No mark::include needed — the accessor marks are the opt-in.
    [[=welder::getter]] int get_code() const { return code_; }
    [[=welder::setter]] void set_code(int c) { code_ = c; }

    // Not included -> not bound (the opt_in control).
    int stray() const { return -1; }

    // A normal method still needs its include.
    [[=welder::mark::include]] int shown() const { return 42; }

  private:
    int code_{1234};
};

// --- flattened non-welded base + a fluent (value-returning) setter -----------

struct Labeled { // NOT welded: a mixin whose accessors flatten into Tag
    [[=welder::getter]] std::string get_label() const { return label_; }
    // Fluent setter: chains in C++, writes plainly from the target language —
    // the returned Labeled& is discarded by every rod (and never gated).
    [[=welder::setter]] Labeled& set_label(std::string v) {
        label_ = std::move(v);
        return *this;
    }

  private:
    std::string label_{"blank"};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Tag : Labeled {
    Tag() = default;
    int id{0};
};

// --- protected accessors under weld_protected --------------------------------

struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::weld_protected
]]
Sealed {
    Sealed() = default;

  protected:
    [[=welder::getter]] int get_inner() const { return inner_; }
    [[=welder::setter]] void set_inner(int v) { inner_ = v; }

  private:
    int inner_{7};
};

} // namespace properties

inline void register_properties(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "properties")};
    WELDER_TEST_WELDER::weld_namespace<^^properties>(sub);
}