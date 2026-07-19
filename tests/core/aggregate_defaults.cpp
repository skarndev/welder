// Aggregate NSDMI-default selection semantics (compile-only; must SUCCEED to
// compile).
//
// The runtime behavior — Python keyword defaults, Lua per-arity constructor
// overloads — is asserted per rod (test_methods.py / methods_spec.lua); locked
// here are the consteval selectors those tests observe only indirectly:
// aggregate_required_arity (the omissible suffix starts after the LAST field
// without an NSDMI — an NSDMI'd field before a required one stays required) and
// aggregate_defaults_from (the value-extracting form: shrinks past
// non-copy-constructible fields, disabled outright when T{} is ill-formed).
// Same compile-to-pass shape as the other core tests; needs only the compiler.
#include <memory>
#include <string>

#include <welder/vocabulary.hpp>  // annotation vocabulary
#include <welder/bind_traits.hpp> // aggregate_required_arity / aggregate_defaults_from

namespace agg {

struct AllRequired {
    int a;
    std::string b;
};

struct AllDefaulted {
    int a{1};
    std::string b{"x"};
};

struct Suffix {
    int samples{4};  // NSDMI, but before a required field -> required
    std::string title;
    int width{800};
    bool resizable{true};
};

struct MoveOnlyTail {
    std::string name;
    std::unique_ptr<int> holder{};  // NSDMI'd but not copyable -> no VALUE default
    int level{1};                   // ...later fields still extract
};

} // namespace agg

using welder::detail::aggregate_defaults_from;
using welder::detail::aggregate_required_arity;

// --- the omissible suffix (arity form: what the Lua rods expose) -------------
static_assert(aggregate_required_arity<agg::AllRequired>() == 2);
static_assert(aggregate_required_arity<agg::AllDefaulted>() == 0);
static_assert(aggregate_required_arity<agg::Suffix>() == 2);
static_assert(aggregate_required_arity<agg::MoveOnlyTail>() == 1);

// --- the value-extracting form (what the Python rods attach) -----------------
// Never ahead of the arity form...
static_assert(aggregate_defaults_from<agg::AllRequired>() == 2);
static_assert(aggregate_defaults_from<agg::AllDefaulted>() == 0);
static_assert(aggregate_defaults_from<agg::Suffix>() == 2);
// ...and shrunk past a non-copyable field: holder is omissible by ARITY (the
// Lua rods may drop it), but its VALUE cannot be copied off a probe, so Python
// defaults start after it.
static_assert(aggregate_defaults_from<agg::MoveOnlyTail>() == 2);