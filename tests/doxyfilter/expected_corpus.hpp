#pragma once
// Corpus for the welder Doxygen INPUT_FILTER — every comment placement and
// every trap the filter must survive, in one file. NB this file is *filter
// input*, it is never compiled: traps may reference nonexistent entities and
// annotations may be syntactically hostile on purpose. The golden test locks
// the filtered text (expected_corpus.hpp); the doxygen end-to-end test (when
// doxygen is installed) locks that the doc texts actually attach.

#include <map>
#include <string>

/** Workshop namespace: the filter test corpus. */
namespace
workshop {

// --- traps: everything in this section must pass through UNCHANGED -----------
// [[=welder::doc("commented out (line) - must not transform")]]
/* block comment: [[=welder::doc("commented out (block) - must not transform")]] */
inline const char* trap_string{"has [[=welder::doc(\"inside a string\")]] in it"};
inline const char* trap_raw{R"(raw [[=welder::returns("inside a raw string")]])"};
inline const char* trap_escaped{"escaped quote \" then [[=welder::doc(\"nope\")]]"};
inline constexpr int trap_separator{1'000'000}; // digit separator is not a char literal
[[nodiscard]] inline int pure_attribute(); // welder-free block: byte-identical

// --- mixed blocks: standard attributes and foreign annotations survive -------
/** Mixed block: nodiscard stays, weld goes. */
struct
[[nodiscard]]
Result {
    /** legacy flag */
    [[deprecated("use v2, not v1")]] bool legacy{false};
    /** foreign annotation kept */
    [[=other::annotation]] int foreign{0};
     int hidden{0};
};

// --- the flagship dedupe case: a template documented as a template on the ----
// C++ side (tparams), whose interior markings also feed runtime docstrings
// when instantiations are bound.
/**
 * An ordered dictionary.
 * @tparam K the key type
 * @tparam V the mapped type
 */
template <class K, class V>
struct
Dict {
    /** number of live entries */ int size{0};
     int cache{0};

    /**
     * Look a key up.
     * @return the mapped value, default-constructed if absent
     */
    V lookup(
         const K& key /**< the key to search */,
         std::map<K, V> defaults = {} /**< fallbacks, keyed like the dict */) const;
};

// --- function template: tparam + doc + returns + parameter docs --------------
template <class T>
/**
 * Clamp a value to a closed range.
 * @tparam T any totally ordered type
 * @return v, lo or hi
 */
T clamp(
     T v /**< the value */,
     T lo /**< lower bound */,
     T hi /**< upper bound */);

// --- enums: keyword position + trailing enumerator docs ----------------------
/** Task state. */
enum class
State {
    Idle /**< nothing queued */,
    Busy,
    Done,
};

// --- parameters with template-argument commas and brace default args ---------
/**
 * Merge two maps.
 * @return the merged map
 */
std::map<int, std::string> merge(
     const std::map<int, std::string>& a /**< the base map */,
     std::map<int, std::string> b = {{1, "one"}} /**< overrides, win on clash */);

// --- single-line keyword positions --------------------------------------------
/** Single-line struct. */
struct  Tag {};

/** Nested helpers. */
namespace  nested {
inline int helper() { return 1; }
} // namespace nested

// weld-only annotation in keyword position: block vanishes, no comment
struct  Quiet {};

} // namespace workshop
