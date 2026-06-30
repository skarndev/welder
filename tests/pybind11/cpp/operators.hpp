#pragma once
// Overloaded operators — mirrors tests/test_operators.py. A member operator binds
// as the matching Python special method (operator+ -> __add__, ...). Exercises
// binary arithmetic, the unary-vs-binary disambiguation of operator-, comparison,
// and subscript.
//
// #included by bindings.cpp after the welder vocabulary + pybind11 backend.

struct
[[=welder::weld(welder::lang::py)]]
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

inline void register_operators(pybind11::module_& m) {
    welder::pybind11::bind<Vec>(m);
}
