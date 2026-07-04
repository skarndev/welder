// Same as ../python_poc, but consuming welder header-only (no `import welder;`).
// This is the fallback path for toolchains with module bugs.
#include <cstdint>
#include <string>

#include <welder/welder.hpp> // annotation vocabulary + reflection (header-only)

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/backends/python/pybind11/backend.hpp> // pybind11 backend

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
    welder::pybind11::bind<Point>(m);
    welder::pybind11::bind<Label>(m);
}
