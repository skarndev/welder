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
#include <welder/rods/python/opaque_containers/document.hpp>

namespace oc = welder::rods::opaque_containers;

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

// --- scalar-eligibility filter (welded-class-element containers are left by value) ---
static_assert(oc::opaque_eligible(^^std::vector<int>));
static_assert(oc::opaque_eligible(^^std::map<std::string, double>));
struct Widget {};
static_assert(!oc::opaque_eligible(^^std::vector<Widget>));            // class element
static_assert(!oc::opaque_eligible(^^std::map<std::string, std::vector<int>>)); // nested

} // namespace
