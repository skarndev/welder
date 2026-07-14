#pragma once
// Cookbook 08 — a stand-in for a THIRD-PARTY library: plain C++, no welder
// annotations anywhere (imagine it comes from a package you don't control).
// Tack welding binds it greedily; see bindings.cpp.
#include <cmath>

namespace vecmath {

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    double norm() const { return std::sqrt(dot(*this)); }

    Vec3 operator+(const Vec3& o) const { return Vec3{x + o.x, y + o.y, z + o.z}; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
};

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

inline constexpr double EPSILON{1e-9};

// Greedy recursion reaches nested namespaces with bindable content too.
namespace units {

inline double to_degrees(double radians) { return radians * 57.29577951308232; }

} // namespace units

} // namespace vecmath