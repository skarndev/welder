#pragma once
// Corpus for the welder Doxygen INPUT_FILTER — every comment placement and
// every trap the filter must survive, in one file. NB this file is *filter
// input*, it is never compiled: traps may reference nonexistent entities and
// annotations may be syntactically hostile on purpose. The golden test locks
// the filtered text (expected_corpus.hpp); the doxygen end-to-end test (when
// doxygen is installed) locks that the doc texts actually attach.

#include <map>
#include <string>

namespace
[[=welder::doc("Workshop namespace: the filter test corpus.")]]
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
struct
[[nodiscard, =welder::weld(welder::lang::py), =welder::doc("Mixed block: nodiscard stays, weld goes.")]]
Result {
    [[deprecated("use v2, not v1"), =welder::doc("legacy flag")]] bool legacy{false};
    [[=other::annotation, =welder::doc("foreign annotation kept")]] int foreign{0};
    [[=welder::mark::exclude]] int hidden{0};
};

// --- the flagship dedupe case: a template documented as a template on the ----
// C++ side (tparams), whose interior markings also feed runtime docstrings
// when instantiations are bound.
template <class K, class V>
struct
[[
  =welder::weld(welder::lang::py),
  =welder::doc("An ordered dictionary."),
  =welder::tparam("K", "the key type"),
  =welder::tparam("V", "the mapped type")
]]
Dict {
    [[=welder::doc("number of live entries")]] int size{0};
    [[=welder::mark::exclude]] int cache{0};

    [[
      =welder::doc("Look a key up."),
      =welder::returns("the mapped value, default-constructed if absent")
    ]]
    V lookup(
        [[=welder::doc("the key to search")]] const K& key,
        [[=welder::doc("fallbacks, keyed like the dict")]] std::map<K, V> defaults = {}) const;
};

// --- function template: tparam + doc + returns + parameter docs --------------
template <class T>
[[
  =welder::doc("Clamp a value to a closed range."),
  =welder::tparam("T", "any totally ordered type"),
  =welder::returns("v, lo or hi")
]]
T clamp(
    [[=welder::doc("the value")]] T v,
    [[=welder::doc("lower bound")]] T lo,
    [[=welder::doc("upper bound")]] T hi);

// --- enums: keyword position + trailing enumerator docs ----------------------
enum class
[[=welder::doc("Task state.")]]
State {
    Idle [[=welder::doc("nothing queued")]],
    Busy [[=welder::mark::exclude]],
    Done,
};

// --- parameters with template-argument commas and brace default args ---------
[[=welder::doc("Merge two maps."), =welder::returns("the merged map")]]
std::map<int, std::string> merge(
    [[=welder::doc("the base map")]] const std::map<int, std::string>& a,
    [[=welder::doc("overrides, win on clash")]] std::map<int, std::string> b = {{1, "one"}});

// --- single-line keyword positions --------------------------------------------
struct [[=welder::doc("Single-line struct.")]] Tag {};

namespace [[=welder::doc("Nested helpers.")]] nested {
inline int helper() { return 1; }
} // namespace nested

// weld-only annotation in keyword position: block vanishes, no comment
struct [[=welder::weld(welder::lang::py)]] Quiet {};

} // namespace workshop
