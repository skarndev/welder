// Compile-only lock for the opaque-container generator's consteval text logic
// (welder::rods::opaque_containers): the derived target NAME and the C++ SPELLING of a
// container, and the scalar-eligibility filter. Pure static_asserts — building this
// target IS the test (tests/CMakeLists.txt: compile.opaque_containers). Covers maps,
// which the runtime gen_opaque group deliberately omits (pybind11-stubgen mis-qualifies
// bind_map view types across submodules; see tests/common/cpp/gen_opaque.hpp).
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <welder/vocabulary.hpp>
#include <welder/rods/python/array_interface.hpp>
#include <welder/rods/python/opaque_containers/document.hpp>

namespace oc = welder::rods::opaque_containers;
namespace ai = welder::rods::python;

namespace {

// Consteval string equality (constexpr std::string::operator== is available here).
consteval bool eq(std::string a, const char* b) { return a == std::string{b}; }

// --- derived names -----------------------------------------------------------
static_assert(eq(oc::derive_name(^^std::vector<int>), "VectorInt"));
static_assert(eq(oc::derive_name(^^std::vector<double>), "VectorDouble"));
static_assert(eq(oc::derive_name(^^std::map<std::string, int>), "MapStringInt"));
static_assert(eq(oc::derive_name(^^std::map<std::string, double>), "MapStringDouble"));
static_assert(eq(oc::derive_name(^^std::unordered_map<int, double>),
                 "UnorderedMapIntDouble"));
static_assert(eq(oc::derive_name(^^std::vector<std::string>), "VectorString"));

// --- C++ spellings (allocator/comparator dropped; std::string is __cxx11 form) ---
static_assert(eq(oc::container_spelling(^^std::vector<int>), "std::vector<int>"));
static_assert(eq(oc::container_spelling(^^std::map<std::string, int>),
                 "std::map<std::__cxx11::basic_string<char>, int>"));

// --- eligibility filter: scalar OR top-level welded class/enum elements ------
static_assert(oc::opaque_eligible(^^std::vector<int>));
static_assert(oc::opaque_eligible(^^std::map<std::string, double>));
// a top-level WELDED class element is eligible (its name is predeclared in phase 1)
struct [[=welder::weld(welder::lang::py)]] Elem { int v{0}; };
static_assert(oc::opaque_eligible(^^std::vector<Elem>));
static_assert(oc::opaque_eligible(^^std::map<int, Elem>));
// a non-welded class element is NOT (the container's gate would reject it anyway)
struct Plain {};
static_assert(!oc::opaque_eligible(^^std::vector<Plain>));
// a nested-in-class welded type is NOT predeclared -> left by value
struct [[=welder::weld(welder::lang::py)]] Host { struct [[=welder::weld(welder::lang::py)]] Inner {}; };
static_assert(!oc::opaque_eligible(^^std::vector<Host::Inner>));
// a nested CONTAINER element is unsafe (inner is a phase-2 binding, not a name)
static_assert(!oc::opaque_eligible(^^std::map<std::string, std::vector<int>>));

// --- numpy array-interface: C++->typestr map + POD-struct eligibility --------
static_assert(eq(ai::numpy_typestr(^^float), "<f4"));
static_assert(eq(ai::numpy_typestr(^^double), "<f8"));
static_assert(eq(ai::numpy_typestr(^^bool), "|b1"));
static_assert(eq(ai::numpy_typestr(^^int), "<i4"));
static_assert(eq(ai::numpy_typestr(^^unsigned char), "|u1"));
static_assert(eq(ai::numpy_typestr(^^long long), "<i8"));
static_assert(eq(ai::numpy_typestr(^^long double), ""));    // no portable dtype
static_assert(eq(ai::numpy_typestr(^^std::string), ""));    // non-arithmetic
struct Vec3 { float x, y, z; };
static_assert(ai::pod_array_eligible<Vec3>());              // POD of arithmetic fields
static_assert(ai::ai_entry_count(^^Vec3) == 3);            // packed: 3 fields, no pad
struct Padded { double d; int i; };                        // {double@0, int@8}, size 16
static_assert(ai::ai_entry_count(^^Padded) == 3);          // d, i, trailing pad(4)
static_assert(!ai::pod_array_eligible<double>());          // a scalar is not a class
struct HasStr { double x; std::string s; };
static_assert(!ai::pod_array_eligible<HasStr>());          // non-arithmetic field
struct HasPtr { double x; int* p; };
static_assert(!ai::pod_array_eligible<HasPtr>());          // pointer field (not numpy)

} // namespace
