#pragma once
#include <welder/detail/config.hpp>

// Intentionally free of any standard-library include: this is exported by the
// `welder` module, and std headers in a module unit's purview/GMF leak into
// every importer (conflicting with their textual std includes on gcc-16).

WELDER_EXPORT namespace welder {

// The set of target languages welder can generate bindings for. `cpp` is a
// placeholder for a future C++-to-C++ adapter; today only `py` has a backend.
enum class lang : unsigned char {
    py,
    lua,
    cpp,
};

} // namespace welder
