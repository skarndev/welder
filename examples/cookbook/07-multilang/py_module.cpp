// Cookbook 07 — the Python side, via the nanobind rod. (Both Python rods speak
// lang::py — welder does not distinguish backends within a language — this recipe
// simply picks nanobind.) The PEP 8 name style reshapes the camelCase C++ names;
// docstrings render Google-style; nanobind's bundled stubgen writes the .pyi.
#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h> // std::string crosses via nanobind's STL casters
#include <welder/rods/python/nanobind/rod.hpp>
#include <welder/rods/python/naming.hpp> // welder::rods::python::pep8

#include "journal.hpp"

namespace nb = nanobind;

NB_MODULE(journal, m) {
    // The same entry point as everywhere else, with the PEP 8 style threaded in:
    // renderLine -> render_line, makeEntry -> make_entry, classes stay PascalCase.
    using weld = welder::welder<welder::rods::nanobind::rod<>,
                                welder::rods::python::pep8>;
    weld::weld_module<^^journal>(m);
}