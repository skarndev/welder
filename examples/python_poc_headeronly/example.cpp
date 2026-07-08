// Same as ../python_poc, but consuming welder header-only (no `import welder;`).
// This is the fallback path for toolchains with module bugs.
#include <cstdint>
#include <string>

#include <welder/vocabulary.hpp> // annotation vocabulary (header-only form)

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/rod.hpp> // pybind11 backend

struct
[[=welder::weld(welder::lang::py)]]
Point {
    double x{0.0};
    double y{0.0};

    [[=welder::mark::exclude(welder::lang::py)]]
    std::uint64_t internal_id{0};
};

struct
[[=welder::weld(welder::lang::py)]]
Label {
    std::string text;

    [[=welder::mark::exclude]]
    std::string cache;
};

PYBIND11_MODULE(welder_poc_ho, m) {
    m.doc() = "welder pybind11 POC (header-only consumption)";
    using weld = welder::welder<welder::rods::pybind11::rod<>>;
    weld::weld_type<Point>(m);
    weld::weld_type<Label>(m);
}
