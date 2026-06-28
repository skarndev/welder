#include <cstdint>
#include <string>

import welder; // annotation vocabulary (module form)

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>     // std::string conversion
#include <welder/python.hpp>  // pybind11 backend (needs the vocabulary above)

// A struct welded for Python. Default policy (automatic) reflects every member
// unless excluded.
struct [[=welder::weld(welder::lang::py)]]
Point {
    double x = 0.0;
    double y = 0.0;

    // Per-language exclusion: hidden from Python.
    [[=welder::mark::exclude(welder::lang::py)]]
    std::uint64_t internal_id = 0;
};

struct [[=welder::weld(welder::lang::py)]]
Label {
    std::string text;
    [[=welder::mark::exclude]] std::string cache; // excluded everywhere
};

PYBIND11_MODULE(welder_poc, m) {
    m.doc() = "welder pybind11 proof-of-concept";
    welder::py::bind<Point>(m);
    welder::py::bind<Label>(m);
}
