#pragma once
// Union cases — mirrors tests/test_unions.py / unions_spec.lua (same sections,
// same order).
//
// Unions NEVER bind in welder: C++ has no way to observe which union member is
// active, so any generated accessor could read an inactive member (undefined
// behavior). Attempting to bind one is a designed hard error — weld_type on a
// union, a weld mark on a union in a swept namespace, and any bound surface
// whose type names a union (see tests/python/pybind11/cpp/neg/union_*.cpp).
// These cases lock the OTHER side of the stance: the escape hatches and the
// blessed path, `std::variant`, which knows its active alternative and crosses
// every rod natively as a value.
//
// The cases live in namespace `unions`, bound under a `unions` submodule.
//
// #included by bindings.cpp after the welder vocabulary + the active backend.

#include <cstdint>
#include <variant>

namespace unions {

// A C-style payload union. Deliberately UNWELDED and UNMARKED: a plain union
// in a swept namespace is skipped (its uses fail the gate instead), so no
// `Payload` appears in the module. (A weld mark on it would be a hard error.)
union Payload {
    std::int32_t i;
    float f;
};

// Named union-typed member: mark::exclude is the escape hatch (unexcluded, the
// bindability gate hard-errors naming std::variant as the fix). The safe-
// accessor remedy rides alongside: `code` reads the member C++ KNOWS is active
// (brace-init of a union initializes its first member).
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Packet {
    int kind{0};
    [[=welder::mark::exclude]] Payload payload{7};

    std::int32_t code() const { return payload.i; }
};

// Anonymous union member: structurally unbindable — there is nothing to name
// the attribute by, and no declarator to carry a mark — so the sweep skips it;
// the named members around it still bind. (The unnamed field also disables the
// synthesized aggregate constructor: it would leak as a positional parameter.)
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Frame {
    int tag{1};
    union {
        std::int32_t raw;
        float scaled;
    };
    int checksum{2};
};

// The blessed path: std::variant crosses as a VALUE in every rod — it arrives
// as the active alternative's natural target value and comes back by matching.
// Boxed is welded, exercising the gate's recursion into variant alternatives.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Boxed {
    std::int32_t n{0};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Holder {
    std::variant<std::int32_t, Boxed> value{};
};

// A variant signature: parameter and return both convert.
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
std::variant<std::int32_t, Boxed> box_if(bool box, std::int32_t n) {
    if (box)
        return Boxed{n};
    return n;
}

} // namespace unions

#ifdef WELDER_TEST_MODULE_T
inline void register_unions(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "unions")};
    WELDER_TEST_WELDER::weld_namespace<^^unions>(sub);
}
#endif