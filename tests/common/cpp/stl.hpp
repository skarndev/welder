#pragma once
// STL-container conversions — mirrors tests/test_stl.py (runtime) and the
// container cases in test_types.mypy-testing (type-level). The conversions
// themselves are the framework casters' job (pybind11's <pybind11/stl.h>;
// nanobind's per-container <nanobind/stl/*.h>, included by its bindings.cpp);
// welder's part is the bindability gate recursing a wrapper's value args (a
// vector<Item> binds iff Item does) and the stub fidelity the typing cases
// assert: a container PARAMETER types as the wide accepted input (Sequence[T] —
// any sequence converts) while a RETURN types as the concrete list[T] /
// dict[K, V] the caster produces.
//
// Python-only, like doc.hpp: the Lua rods do not include this group (mypy is
// the audience; the Lua frameworks' container semantics are their own story).
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace stl {

// A welded element type: a container of a *registered* class must recurse the
// gate down to Item and spell the bound name in the stub (list[Item], never a
// raw C++ spelling).
struct [[=welder::weld(welder::lang::py)]] Item {
    int id{0};

    Item() = default;
    Item(int i) : id{i} {}
};

// --- vector<T> <-> list ------------------------------------------------------
[[=welder::weld(welder::lang::py)]]
std::vector<int> iota(int n) {
    std::vector<int> values{};
    for (int i{0}; i < n; ++i) {
        values.push_back(i);
    }
    return values;
}

[[=welder::weld(welder::lang::py)]]
int total(const std::vector<int>& values) {
    int sum{0};
    for (int v : values) {
        sum += v;
    }
    return sum;
}

[[=welder::weld(welder::lang::py)]]
std::vector<Item> wrap_all(const std::vector<int>& ids) {
    std::vector<Item> items{};
    items.reserve(ids.size());
    for (int i : ids) {
        items.emplace_back(i);
    }
    return items;
}

// --- map<K, V> <-> dict ------------------------------------------------------
[[=welder::weld(welder::lang::py)]]
std::map<std::string, int> histogram(const std::vector<std::string>& words) {
    std::map<std::string, int> counts{};
    for (const std::string& w : words) {
        ++counts[w];
    }
    return counts;
}

// --- optional<T> <-> T | None ------------------------------------------------
[[=welder::weld(welder::lang::py)]]
std::optional<int> find_positive(const std::vector<int>& values) {
    for (int v : values) {
        if (v > 0) {
            return v;
        }
    }
    return std::nullopt;
}

[[=welder::weld(welder::lang::py)]]
int value_or(std::optional<int> value, int fallback) {
    return value.value_or(fallback);
}

// --- pair <-> tuple ----------------------------------------------------------
[[=welder::weld(welder::lang::py)]]
std::pair<int, std::string> labelled(int n) {
    return {n, std::to_string(n)};
}

// --- container-typed data members -> properties ------------------------------
// The property getter types as the concrete list/dict; the setter accepts the
// wide Sequence/Mapping form, so `basket.ints = [1, 2]` typechecks.
struct [[=welder::weld(welder::lang::py)]] Basket {
    std::vector<int> ints{};
    std::map<std::string, int> ranks{};
    std::optional<double> discount{};
};

} // namespace stl

inline void register_stl(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "stl")};
    WELDER_TEST_WELDER::weld_namespace<^^stl>(sub);
}