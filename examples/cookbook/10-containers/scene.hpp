#pragma once
// Cookbook 10 — the welded types. Plain std::vector members: NO WELDER_OPAQUE, NO
// aliases. The generator (gen.cpp) reflects these and emits scene.opaque.hpp, which
// binds each container opaquely (by reference). See docs/content/cookbook/containers.md.
#include <string>
#include <utility>
#include <vector>

#include <welder/vocabulary.hpp>
#include <welder/rods/python/opaque_containers/marks.hpp> // by_value opt-out

namespace scene {

// A plain-old-data vertex — its opaque vector also gets a numpy STRUCTURED view.
struct [[=welder::weld(welder::lang::py)]] Vertex {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

// A rich welded class (holds a std::string) — reference semantics, but not a POD, so
// no numpy view. Made bindable by the driver's two-phase sweep (its name is registered
// before std::vector<Entity> binds), which is what lets a container of a welded class
// be opened opaque at all.
struct [[=welder::weld(welder::lang::py)]] Entity {
    std::string name{};
    double health{100.0};

    Entity() = default;
    Entity(std::string n, double h) : name{std::move(n)}, health{h} {}
};

struct [[=welder::weld(welder::lang::py)]] Scene {
    std::vector<Vertex> mesh{};      // opaque; POD  -> np.asarray => structured array
    std::vector<Entity> actors{};    // opaque; vector<welded class> (two-phase)
    std::vector<double> weights{};   // opaque; scalar -> np.asarray => float64 array
    // opt out: keep this one a plain Python list[int] (no WELDER_OPAQUE emitted for it)
    [[=welder::rods::python::by_value]] std::vector<int> layers{};
};

// Round-trip helpers so the check script can prove a Python-side append reached C++.
[[=welder::weld(welder::lang::py)]]
double total_weight(const Scene& s) {
    double t{0.0};
    for (double w : s.weights)
        t += w;
    return t;
}

[[=welder::weld(welder::lang::py)]]
std::size_t actor_count(const Scene& s) {
    return s.actors.size();
}

} // namespace scene
