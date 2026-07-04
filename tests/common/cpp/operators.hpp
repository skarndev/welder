#pragma once
// Overloaded operators — mirrors tests/test_operators.py. A member operator binds
// as the matching Python special method (operator+ -> __add__, ...). Exercises
// binary arithmetic, the unary-vs-binary disambiguation of operator-, comparison,
// and subscript.
//
// The cases live in namespace `operators`, bound under an `operators` submodule
// via WELDER_TEST_BE::bind_namespace so the Python package mirrors this file.
// (The free operator+ below is non-welded, so bind_namespace skips it — which is
// exactly the "free operators aren't bound yet" case it documents.)
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.

namespace operators {

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Vec {
    double x{0.0};
    double y{0.0};

    Vec() = default;
    Vec(double x_, double y_) : x{x_}, y{y_} {}

    Vec operator+(const Vec& o) const { return {x + o.x, y + o.y}; } // __add__
    Vec operator-(const Vec& o) const { return {x - o.x, y - o.y}; } // __sub__ (binary)
    Vec operator-() const { return {-x, -y}; }                       // __neg__ (unary)
    Vec operator*(double s) const { return {x * s, y * s}; }         // __mul__

    bool operator==(const Vec& o) const { return x == o.x && y == o.y; } // __eq__
    bool operator!=(const Vec& o) const { return !(*this == o); }        // __ne__

    // __getitem__; the overload is spliced individually, so no &Vec::operator[]
    // ambiguity even if a non-const overload were added.
    double operator[](int i) const { return i == 0 ? x : y; }

    // operator= is a special member -> never bound (Python assignment isn't
    // overloadable); present just to confirm it is skipped.
    Vec& operator=(const Vec&) = default;
};

// --- Heterogeneous operators: the right-hand operand is a *different* type.
// Three cases exercised below: the other type welded, the other type NOT welded,
// and a free (non-member) operator defined separately from the class.

// Case 1: the other operand is itself welded. Its member operator takes a Feet,
// and because Feet is registered with pybind11 the dunder converts it and works.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Feet {
    double value{0.0};
    Feet() = default;
    explicit Feet(double v) : value{v} {}
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Meters {
    double value{0.0};
    Meters() = default;
    explicit Meters(double v) : value{v} {}
    // __add__(self, Feet) -> Meters. Feet is welded, so pybind11 has a converter.
    Meters operator+(const Feet& f) const { return Meters{value + f.value * 0.3048}; }
};

// Case 2: the other operand is NOT welded. Binding such an operator would produce
// __add__(self, RawTag) with no pybind11 converter for RawTag — a dead dunder
// whose stub references an unimportable type. welder's bindability gate rejects it
// at compile time (a hard error), so it cannot be exercised at runtime; the
// compile-failure is pinned by the negcompile.operand_not_welded CTest
// (tests/pybind11/cpp/neg/operand_not_welded.cpp). Shown here for context:
//
//   struct RawTag { int id{0}; };                        // no weld
//   struct [[=welder::weld(welder::lang::py, welder::lang::lua)]] Tagged {
//       int id{0};
//       Tagged operator+(const RawTag&) const;           // -> hard compile error
//   };

// Case 3: the operator is defined *separately* from the class as a free,
// non-member function (legal C++). welder scans only a type's own members, so a
// free operator is not discovered and __add__ never appears. Documents the
// current "free operators aren't bound yet" limitation.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Coin {
    int cents{0};
    Coin() = default;
    explicit Coin(int c) : cents{c} {}
};
inline Coin operator+(const Coin& a, const Coin& b) { return Coin{a.cents + b.cents}; }

// --- Operators honor the same exclude/include/policy resolution as methods. ---
// is_bindable_operator consults member_bound (the shared resolver), so a marked
// operator binds exactly as a marked method would. These two structs prove the
// operator path actually consults it: an excluded operator's dunder disappears,
// and under opt_in an operator binds only when explicitly included.

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
[[=welder::policy::automatic]]
OpAutomatic {
    int v{0};
    OpAutomatic() = default;
    explicit OpAutomatic(int x) : v{x} {}
    OpAutomatic operator+(const OpAutomatic& o) const { return OpAutomatic{v + o.v}; } // __add__ (bound)
    [[=welder::mark::exclude]]
    OpAutomatic operator*(const OpAutomatic& o) const { return OpAutomatic{v * o.v}; } // __mul__ (excluded)
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
[[=welder::policy::opt_in]]
OpOptIn {
    [[=welder::mark::include]] int v{0};  // included so the test can read a result
    OpOptIn() = default;
    explicit OpOptIn(int x) : v{x} {}
    [[=welder::mark::include]]
    OpOptIn operator+(const OpOptIn& o) const { return OpOptIn{v + o.v}; } // __add__ (included -> bound)
    OpOptIn operator-(const OpOptIn& o) const { return OpOptIn{v - o.v}; } // __sub__ (unmarked -> not bound)
};

} // namespace operators

inline void register_operators(WELDER_TEST_MODULE_T& m) {
    // Declaration order binds Feet before Meters (Meters::operator+ takes a Feet).
    auto sub{WELDER_TEST_SUBMODULE(m, "operators")};
    WELDER_TEST_BE::bind_namespace<^^operators>(sub);
}
