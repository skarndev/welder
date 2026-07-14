// Cookbook 07 — the Python side, via the nanobind rod. (Both Python rods speak
// lang::py — welder does not distinguish backends within a language — this recipe
// simply picks nanobind.) The PEP 8 name style reshapes the camelCase C++ names;
// docstrings render Google-style; nanobind's bundled stubgen writes the .pyi.
//
// WELDER_MODULE's optional third argument is the exact welder<> to drive the
// weld with — the way to thread a name style through the one-line module form.
#include <welder/vocabulary.hpp>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h> // std::string crosses via nanobind's STL casters
#include <welder/rods/python/nanobind/module.hpp>
#include <welder/rods/python/naming.hpp> // welder::rods::python::pep8

#include "journal.hpp"

WELDER_MODULE(journal, nanobind,
              welder::welder<welder::rods::nanobind::rod<>,
                             welder::rods::python::pep8>) {}