#pragma once

// welder is C++26-and-newer only and is built on P2996 reflection. On the one
// toolchain that implements it today, gcc-16, that means `-std=c++26 -freflection`,
// which together define __cpp_impl_reflection. We check the capability here — the
// first header any welder TU pulls in (via <welder/vocabulary.hpp>) — so a
// misconfigured target fails with this one clear message instead of a wall of
// syntax errors deeper in the machinery. welder deliberately does NOT force the C++
// standard onto your target from CMake (no INTERFACE cxx_std_26): the language level
// is your project's dial to set. (Broaden the macro check as other compilers land
// P2996.)
#if !defined(__cpp_impl_reflection)
#  error "welder requires C++26 reflection. On gcc-16 build this target with " \
         "`-std=c++26 -freflection` (set CMAKE_CXX_STANDARD to 26 or newer). " \
         "See the welder \"Getting started\" guide."
#endif

/** @file
    Target-language vocabulary — the set of languages welder can bind to.

    @note Intentionally free of any standard-library include. welder ships
    header-only today, but the vocabulary is kept std-include-free so it can be
    re-exported by a future `welder` C++20 module without a std-leak: std headers
    in a module unit's purview/GMF leak into every importer (conflicting with
    their textual std includes on gcc-16). See the "Header-only for now" page in
    the docs for why modularization is deferred.
*/

namespace welder::inline v0 {

/** The set of target languages welder can generate bindings for.

    Stored as a bit index in an `unsigned` mask by the annotation vocabulary.
    Both values have backends today (`py`: pybind11 + nanobind; `lua`: sol2).
*/
enum class lang : unsigned char {
    py,   /**< Python (via the pybind11 and nanobind backends). */
    lua,  /**< Lua (via the sol2 backend). */
};

} // namespace welder
