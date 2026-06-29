#pragma once
//
// Header-only entry point for welder's core: the annotation vocabulary plus the
// reflection-based resolution. Include this (instead of `import welder;`) when
// you can't or don't want to use the module — e.g. to work around a compiler
// modules bug. Backends are separate headers (e.g. <welder/backends/pybind11.hpp>).
//
#include <welder/lang.hpp>
#include <welder/annotations.hpp>
#include <welder/reflect.hpp>
