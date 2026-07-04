#pragma once
/** @file
    Header-only umbrella for welder's core: vocabulary + reflection.

    Include this (instead of `import welder;`) when you can't or don't want to use
    the module — e.g. to work around a compiler modules bug. It pulls in the
    annotation vocabulary plus the reflection-based resolution. Backends are
    separate headers (e.g. `<welder/backends/python/pybind11/backend.hpp>`).
*/

#include <welder/lang.hpp>
#include <welder/annotations.hpp>
#include <welder/reflect.hpp>
#include <welder/doc.hpp>
