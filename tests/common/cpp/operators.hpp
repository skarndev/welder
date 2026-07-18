#pragma once
// Overloaded operators — mirrors tests/test_operators.py. A member operator binds
// as the matching Python special method (operator+ -> __add__, ...). Exercises
// binary arithmetic, the unary-vs-binary disambiguation of operator-, comparison,
// subscript, FREE (namespace-scope) operators anchored on a welded type (plain,
// mixed member+free slots, reflected right-operand, the ostream stringifier),
// and the comparisons synthesized from operator<=>.
//
// The cases live in namespace `operators`, bound under an `operators` submodule
// via WELDER_TEST_WELDER::weld_namespace so the Python package mirrors this file.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.
#include <compare>
#include <ostream>

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
// non-member function. welder sweeps the welded type's enclosing namespace for
// operators ANCHORED on it (an operand IS the type), so the free operator+
// binds as __add__ exactly like a member one — and a marked one resolves like
// any member (the excluded operator- below never appears).
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Coin {
    int cents{0};
    Coin() = default;
    explicit Coin(int c) : cents{c} {}
};
inline Coin operator+(const Coin& a, const Coin& b) { return Coin{a.cents + b.cents}; }
[[=welder::mark::exclude]]
inline Coin operator-(const Coin& a, const Coin& b) { return Coin{a.cents - b.cents}; }

// A member and a free operator sharing one slot: the carriage combines them into
// ONE group per (operator, arity), so both bind — critical on the Lua rods,
// which store one value per metamethod slot (two separate registrations would
// clobber; one sol::overload / variadic addFunction holds both).
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Mixed {
    int v{0};
    Mixed() = default;
    explicit Mixed(int x) : v{x} {}
    Mixed operator+(const Mixed& o) const { return Mixed{v + o.v}; } // member
};
inline Mixed operator+(const Mixed& a, int b) { return Mixed{a.v + b}; } // free, same slot

// Reflected operand + stringifier. operator*(double, Scaled) has the welded type
// on the RIGHT: the Python rods bind it as __rmul__ (through an operand-swapping
// wrapper) so `2.0 * s` works; the Lua rods just add the exact (number, Scaled)
// signature to __mul (Lua passes a metamethod its operands as written). The free
// ostream inserter becomes Python __str__ / Lua __tostring.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Scaled {
    double f{1.0};
    Scaled() = default;
    explicit Scaled(double v) : f{v} {}
};
inline Scaled operator*(const Scaled& s, double k) { return Scaled{s.f * k}; }
inline Scaled operator*(double k, const Scaled& s) { return Scaled{k * s.f}; }
inline std::ostream& operator<<(std::ostream& os, const Scaled& s) {
    return os << "Scaled(" << s.f << ")";
}

// --- operator<=> synthesizes the relational slots. --------------------------
// The spaceship itself never binds (std::*_ordering does not cross); welder
// synthesizes rewritten expressions (`a < b`, ...) instead: __lt__/__le__/
// __gt__/__ge__ on Python, __lt/__le on Lua (which derives >, >=, ~= itself).

// A DEFAULTED spaceship also implicitly declares a defaulted operator==, which
// binds through the ordinary operator path — so Version gets the full set.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Version {
    // (not `major`/`minor` — glibc leaks those as macros)
    int maj{0};
    int mnr{0};
    Version() = default;
    Version(int mj, int mn) : maj{mj}, mnr{mn} {}
    auto operator<=>(const Version&) const = default;
};

// A CUSTOM spaceship synthesizes the four relationals but NOT == (C++ itself
// only rewrites == from operator==; none is declared here).
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Temp {
    double celsius{0.0};
    Temp() = default;
    explicit Temp(double c) : celsius{c} {}
    std::strong_ordering operator<=>(const Temp& o) const {
        return celsius < o.celsius   ? std::strong_ordering::less
               : celsius > o.celsius ? std::strong_ordering::greater
                                     : std::strong_ordering::equal;
    }
};

// A HETEROGENEOUS spaceship: comparisons against a plain int, both directions
// (Python routes `5 < a` through the reflected protocol — the synthesized slots
// return NotImplemented on an operand mismatch; Lua consults either operand's
// metamethod and the synthesis registers both operand orders).
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Account {
    int balance{0};
    Account() = default;
    explicit Account(int b) : balance{b} {}
    std::weak_ordering operator<=>(int b) const { return balance <=> b; }
    bool operator==(int b) const { return balance == b; }
};

// An EXPLICIT relational operator beats synthesis, slot by slot (mirroring
// C++'s preference for non-rewritten candidates): the deliberately INVERTED
// operator< is what < binds, while >, <=, >= still synthesize from <=> — the
// exact asymmetry a C++ caller sees.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Ordered {
    int rank{0};
    Ordered() = default;
    explicit Ordered(int r) : rank{r} {}
    bool operator<(const Ordered& o) const { return rank > o.rank; } // inverted!
    std::strong_ordering operator<=>(const Ordered& o) const {
        return rank <=> o.rank;
    }
};

// Marks on the spaceship scope the synthesis per language like any member:
// Python compares, Lua does not.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
PyOnlyCmp {
    int v{0};
    PyOnlyCmp() = default;
    explicit PyOnlyCmp(int x) : v{x} {}
    [[=welder::mark::exclude(welder::lang::lua)]]
    auto operator<=>(const PyOnlyCmp&) const = default;
};

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
    // Constructors resolve symmetrically under opt_in (see overloads.hpp), so the
    // value ctor the specs use is opted in like everything else.
    [[=welder::mark::include]]
    explicit OpOptIn(int x) : v{x} {}
    [[=welder::mark::include]]
    OpOptIn operator+(const OpOptIn& o) const { return OpOptIn{v + o.v}; } // __add__ (included -> bound)
    OpOptIn operator-(const OpOptIn& o) const { return OpOptIn{v - o.v}; } // __sub__ (unmarked -> not bound)
};

} // namespace operators

inline void register_operators(WELDER_TEST_MODULE_T& m) {
    // Declaration order binds Feet before Meters (Meters::operator+ takes a Feet).
    auto sub{WELDER_TEST_SUBMODULE(m, "operators")};
    WELDER_TEST_WELDER::weld_namespace<^^operators>(sub);
}
