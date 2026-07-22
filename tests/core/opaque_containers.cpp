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

// --- names for class-template-specialization elements (NTTP args) ------------
// These previously fell to display_string_of and produced INVALID C++ (a name with
// '::' '<' '>' '{' '}' ','); they must now be valid readable identifiers. The final
// add_one sanitize is the guarantee, so assert on sanitize_ident(derive_name(...)).
template <int N> struct Cell {};
static_assert(eq(oc::sanitize_ident(oc::derive_name(^^std::vector<Cell<1>>)),
                 "VectorCell1"));
static_assert(eq(oc::sanitize_ident(oc::derive_name(^^std::map<int, Cell<7>>)),
                 "MapIntCell7"));
struct Ver { int a, b, c, d; };            // structural -> usable as an NTTP
template <Ver V> struct Grp {};
// exact spelling of the value part is compiler-specific; assert it is a valid, unique
// identifier (no non-identifier char, no leading digit) — the safety-net contract.
consteval bool valid_ident(std::string s) {
    if (s.empty() || (s[0] >= '0' && s[0] <= '9'))
        return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}
static_assert(valid_ident(
    oc::sanitize_ident(oc::derive_name(^^std::vector<Grp<Ver{3, 3, 5, 12340}>>))));

// --- sanitize_ident: the last-resort identifier guarantee --------------------
static_assert(eq(oc::sanitize_ident("VectorInt"), "VectorInt"));       // clean pass-through
static_assert(eq(oc::sanitize_ident("a::b<c>{1, 2}"), "a_b_c_1_2"));   // collapse runs
static_assert(eq(oc::sanitize_ident("123abc"), "_123abc"));           // leading digit
static_assert(eq(oc::sanitize_ident("<<>>"), "T"));                   // all junk -> T

// --- collision-free qualified names (part a) ---------------------------------
// A namespaced element is qualified by its namespace path (std dropped), so two
// same-named types in different namespaces derive DISTINCT names.
namespace geo {
struct [[=welder::weld(welder::lang::py)]] Point { double x, y; };
}
namespace physics {
struct [[=welder::weld(welder::lang::py)]] Point { double p, q; };
}
static_assert(eq(oc::qualified_ident(^^geo::Point), "GeoPoint"));
static_assert(eq(oc::qualified_ident(^^physics::Point), "PhysicsPoint"));
static_assert(eq(oc::derive_name(^^std::vector<geo::Point>), "VectorGeoPoint"));
static_assert(eq(oc::derive_name(^^std::vector<physics::Point>), "VectorPhysicsPoint"));
// the two no longer collide (bare "VectorPoint" for both, pre-fix)
static_assert(oc::derive_name(^^std::vector<geo::Point>) !=
              oc::derive_name(^^std::vector<physics::Point>));
// enclosing classes qualify too (a nested type reads Outer.Inner-style)
struct Outer { struct Inner {}; };
static_assert(eq(oc::qualified_ident(^^Outer::Inner), "OuterInner"));
// std / fundamentals stay clean (std namespace + global are dropped)
static_assert(eq(oc::qualified_ident(^^int), "Int"));
static_assert(eq(oc::derive_name(^^std::map<geo::Point, int>), "MapGeoPointInt"));

// --- custom naming hook on the name style (part b) ---------------------------
// A style may override the derived name via an optional transform_opaque_container
// hook receiving (enclosing, container, member); a style without it falls through to
// derive_name. The hook's result is sanitized like the default.
struct Bag {
    std::vector<geo::Point> pts;
};
struct my_style : welder::naming::none {
    static consteval std::string transform_opaque_container(std::meta::info /*encl*/,
                                                            std::meta::info /*cont*/,
                                                            std::meta::info member) {
        return "My" + std::string{std::meta::identifier_of(member)};
    }
};
// default style: opaque_name == the collision-free derive_name
static_assert(eq(oc::opaque_name<welder::naming::none, ^^Bag,
                                 ^^std::vector<geo::Point>, ^^Bag::pts>(),
                 "VectorGeoPoint"));
// custom style: the hook wins, keyed off the member reflection (member id is "pts")
static_assert(eq(oc::opaque_name<my_style, ^^Bag, ^^std::vector<geo::Point>,
                                 ^^Bag::pts>(),
                 "Mypts"));

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
