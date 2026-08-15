#pragma once
// C++ DEFAULT ARGUMENTS — mirrors tests/test_defaults.py.
//
// Reflection can see that a parameter HAS a default (`has_default_argument`)
// but cannot splice the defaulting expression, so the Python rods bind one
// TRUNCATED overload per omissible arity: the wrapper calls the C++ function
// with fewer arguments and the language applies the real default at the call
// site — the bound behavior and the C++ default can never drift apart.
//
// #included by bindings.cpp after the welder vocabulary + the active backend.
#include <string>

namespace defaults {

struct
[[=welder::weld]]
Dial {
    int value{0};
    std::string label;

    Dial() = default;

    // Constructor with a trailing default: Dial(3) and Dial(3, "x") both work.
    Dial(int start, std::string name = "dial") : value{start}, label{std::move(name)} {}

    // TWO trailing defaults -> three callable arities.
    int bump(int by = 1, int times = 1) {
        value += by * times;
        return value;
    }

    // A defaulted parameter after a required one.
    int scaled(int base, int factor = 10) const {
        return base * factor + value;
    }

    // Static method with a default.
    static int stride(int step = 4) {
        return step * 2;
    }
};

// Free function with a trailing default.
[[=welder::weld]]
inline int spaced(int a, int gap = 100) {
    return a + gap;
}

} // namespace defaults

inline void register_defaults(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "defaults")};
    WELDER_TEST_WELDER::weld_namespace<^^defaults>(sub);
}
