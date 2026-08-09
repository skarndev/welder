#pragma once
// Cookbook 11 — C#/.NET bindings. One welded class with a live container
// member, a nested namespace (a real C# namespace on the other side), and a
// root-level free function. See docs/content/cookbook/csharp.md.
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include <welder/vocabulary.hpp>

namespace inventory {

struct [[=welder::weld(welder::lang::cs)]]
[[=welder::doc("A shipping crate.")]]
Crate {
    std::string label{};
    std::int32_t weight{0};
    // A live member: C# sees a reference-semantic VectorInt whose AsSpan()
    // is a zero-copy view over this very buffer.
    std::vector<std::int32_t> serials{};

    Crate() = default;
    Crate(std::string l, std::int32_t w) : label{std::move(l)}, weight{w} {}

    std::int32_t serial_total() const {
        return std::accumulate(serials.begin(), serials.end(), 0);
    }
};

[[=welder::weld(welder::lang::cs)]]
inline std::int32_t combined_weight(const Crate& a, const Crate& b) {
    return a.weight + b.weight;
}

// A nested namespace: C# gets `inventory.Audit` as a real namespace, its free
// functions on `inventory.Audit.Global`.
namespace audit {

[[=welder::weld(welder::lang::cs)]]
inline std::string stamp(const Crate& c) {
    return c.label + "#" + std::to_string(c.weight);
}

} // namespace audit

} // namespace inventory
