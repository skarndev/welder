#pragma once

/** @file
    Target-language vocabulary — the set of languages welder can bind to.

    @note Intentionally free of any standard-library include. welder ships
    header-only today, but the vocabulary is kept std-include-free so it can be
    re-exported by a future `welder` C++20 module without a std-leak: std headers
    in a module unit's purview/GMF leak into every importer (conflicting with
    their textual std includes on gcc-16). See the "Header-only for now" page in
    the docs for why modularization is deferred.
*/

namespace welder {

/** The set of target languages welder can generate bindings for.

    Stored as a bit index in an `unsigned` mask by the annotation vocabulary.
    Both values have backends today (`py`: pybind11 + nanobind; `lua`: sol2).
*/
enum class lang : unsigned char {
    py,   /**< Python (via the pybind11 and nanobind backends). */
    lua,  /**< Lua (via the sol2 backend). */
};

} // namespace welder
