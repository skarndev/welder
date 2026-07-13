#pragma once

/** @file
    welder's version identity and ABI namespace.

    The three version components below are the **single source of truth**: the
    top-level CMakeLists.txt parses them for `project(welder VERSION …)` and
    conanfile.py reads them the same way — bump them here only.

    @note Intentionally free of any standard-library include (like the rest of
    the vocabulary), and free of any reflection use — safe to include from a
    TU compiled under any language level when all you need is the version.
*/

#define WELDER_VERSION_MAJOR 0  /**< welder's major version component. */
#define WELDER_VERSION_MINOR 1  /**< welder's minor version component. */
#define WELDER_VERSION_PATCH 0  /**< welder's patch version component. */

/** One monotonically comparable number: `major * 1'000'000 + minor * 1'000 +
    patch` — `#if WELDER_VERSION >= 1000` reads "at least 0.1.0". */
#define WELDER_VERSION (WELDER_VERSION_MAJOR * 1000000 \
                        + WELDER_VERSION_MINOR * 1000  \
                        + WELDER_VERSION_PATCH)

#define WELDER_DETAIL_VERSION_STR2(x) #x
#define WELDER_DETAIL_VERSION_STR(x) WELDER_DETAIL_VERSION_STR2(x)

/** The version as a string literal, `"major.minor.patch"`. */
#define WELDER_VERSION_STRING                          \
    WELDER_DETAIL_VERSION_STR(WELDER_VERSION_MAJOR) "." \
    WELDER_DETAIL_VERSION_STR(WELDER_VERSION_MINOR) "." \
    WELDER_DETAIL_VERSION_STR(WELDER_VERSION_PATCH)

/** The ABI namespace: every `welder::` name actually lives in this *inline*
    namespace (`namespace welder::inline v0 { … }` throughout the headers).

    Inline, so it is invisible in source — you spell `welder::weld`,
    `welder::welder<Rod>` as ever — but it *is* part of the mangled symbol
    names. welder is header-only: every TU instantiates its own copy of the
    templates it uses, and when two libraries built against *different* welder
    versions are linked into one binary, the linker would otherwise silently
    merge those (weak) symbols across versions — an ODR violation with
    undefined behavior. Versioned, the two sets of symbols stay distinct: each
    library runs the welder it was built with, and mixing welder types across
    the two fails loudly (different types) instead of corrupting silently.

    Bumped only on ABI-breaking releases, not on every version. The headers
    spell the namespace literally (`v0`); this macro is for code that needs to
    *name* the ABI namespace programmatically, and the guard below keeps the
    two in sync.
*/
#define WELDER_ABI_NAMESPACE v0

namespace welder::inline WELDER_ABI_NAMESPACE {

/** welder's version as `constexpr` integers, for C++-level dispatch (the
    macros above serve the preprocessor). */
inline constexpr int version_major{WELDER_VERSION_MAJOR};
inline constexpr int version_minor{WELDER_VERSION_MINOR};  /**< @see version_major */
inline constexpr int version_patch{WELDER_VERSION_PATCH};  /**< @see version_major */

} // namespace welder

// Drift guard: the headers spell the ABI namespace literally (`v0`); if
// WELDER_ABI_NAMESPACE ever diverges from them, the constant above lands in a
// different inline namespace and this name fails to resolve.
static_assert(::welder::v0::version_major == WELDER_VERSION_MAJOR,
              "WELDER_ABI_NAMESPACE is out of sync with the `v0` the headers spell");