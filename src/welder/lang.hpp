#pragma once
#include <welder/detail/config.hpp>

// Intentionally free of any standard-library include: this is exported by the
// `welder` module, and std headers in a module unit's purview/GMF leak into
// every importer (conflicting with their textual std includes on gcc-16).

WELDER_EXPORT namespace welder {

// The set of target languages welder can generate bindings for.
// today only `py` has a backend.
enum class lang : unsigned char {
    py,
    lua,
    // The C++ *documentation* surface, a pseudo-target consumed by the docs
    // pipeline (<welder/docs.hpp>) rather than a binding backend. C++ needs no
    // binding — every public entity is implicitly "welded" to it — so `weld`
    // never gates it and `policy` does not apply (always automatic); the only
    // control is mark::exclude(lang::cxx_doc) to hide an entity from the
    // generated C++ reference.
    cxx_doc,
};

} // namespace welder
