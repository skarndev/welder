#pragma once
// Cookbook 09 — a stand-in for a THIRD-PARTY library: plain C++, no welder
// annotations, and — like most real libraries — a *convention* for what is
// private: underscore-prefixed names and a `detail` namespace. Plain tack
// welding would greedily bind all of it; see bindings.cpp for the custom
// resolution that prunes by the library's own convention.
namespace sensorlib {

struct Reading {
    double celsius{21.5};

    bool fresh() const { return true; }
};

// Library-internal by convention: the underscore prefix.
struct _CalibrationTable {
    int rows{64};
};

inline Reading take_reading() { return Reading{}; }

inline void _reset_driver() {}

inline constexpr int API_LEVEL{3};

inline int _debug_flag{0};

// The library's private namespace: greedy recursion would happily descend into
// it and expose the driver guts.
namespace detail {

struct Driver {
    int handle{7};
};

inline int open_driver() { return 1; }

} // namespace detail

// A public nested namespace stays a submodule.
namespace units {

inline double to_fahrenheit(double c) { return c * 9.0 / 5.0 + 32.0; }

} // namespace units

} // namespace sensorlib