#pragma once
#include <welder/detail/config.hpp>

/** @file
    Target-language vocabulary — the set of languages welder can bind to.

    @note Intentionally free of any standard-library include: this header is
    exported by the `welder` module, and std headers in a module unit's
    purview/GMF leak into every importer (conflicting with their textual std
    includes on gcc-16).
*/

WELDER_EXPORT namespace welder {

/** The set of target languages welder can generate bindings for.

    Stored as a bit index in an `unsigned` mask by the annotation vocabulary.
    Both values have backends today (`py`: pybind11 + nanobind; `lua`: sol2).
*/
enum class lang : unsigned char {
    py,   /**< Python (via the pybind11 and nanobind backends). */
    lua,  /**< Lua (via the sol2 backend). */
};

} // namespace welder
