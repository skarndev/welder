// C# rod marshalling/consteval locks (compile-only; must SUCCEED to compile).
//
// static_asserts over the C# rod's consteval text layer — the classification,
// the paired C-ABI/C# scalar spellings, the symbol mangling and the shared
// member-lookup layer (the generator↔shim agreement contract). These lock the
// exact strings/reflections the golden files depend on, without running the
// generator. Compiled by the `compile.csharp_marshal` CTest (a plain build
// target, no WILL_FAIL) — needs only the compiler, no .NET.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>
#include <welder/rods/csharp/rod.hpp>

namespace {

namespace wcs = ::welder::rods::csharp;
using wcs::marshal_kind;

// A small welded surface to reflect over.
namespace lockcases {
struct [[=welder::weld(welder::lang::cs)]] Thing {
    std::int32_t n{0};
    std::string s{};
    Thing() = default;
    Thing(std::int32_t v) : n{v} {}
    Thing(std::int32_t v, std::int32_t w) : n{v + w} {}
    std::int32_t get() const { return n; }
    std::int32_t get(std::int32_t base) const { return n + base; } // overload
};
enum class [[=welder::weld(welder::lang::cs)]] Tag : std::uint16_t { A, B };
struct NotWelded {};
[[=welder::weld(welder::lang::cs)]] inline std::int32_t leaf(std::int32_t v) {
    return v;
}
[[=welder::weld(welder::lang::cs)]] inline std::int32_t stripped(
    std::int32_t _err_slot) {
    return _err_slot;
}
} // namespace lockcases

consteval bool streq(const char* a, std::string_view b) { return a && a == b; }

// --- classification ---------------------------------------------------------
static_assert(wcs::classify(^^void) == marshal_kind::void_);
static_assert(wcs::classify(^^int) == marshal_kind::scalar);
static_assert(wcs::classify(^^const double&) == marshal_kind::scalar);
static_assert(wcs::classify(^^bool) == marshal_kind::boolean);
static_assert(wcs::classify(^^std::string) == marshal_kind::utf8_string);
static_assert(wcs::classify(^^const std::string&) == marshal_kind::utf8_string);
static_assert(wcs::classify(^^std::string_view) == marshal_kind::utf8_string);
static_assert(wcs::classify(^^const char*) == marshal_kind::utf8_string);
static_assert(wcs::classify(^^lockcases::Tag) == marshal_kind::enum_);
static_assert(wcs::classify(^^lockcases::Thing) == marshal_kind::handle);
static_assert(wcs::classify(^^const lockcases::Thing&) == marshal_kind::handle);
static_assert(wcs::classify(^^lockcases::Thing*) == marshal_kind::handle);
static_assert(wcs::is_pointer_flavor(^^lockcases::Thing*));
static_assert(!wcs::is_pointer_flavor(^^const lockcases::Thing&));

// --- the container families ---------------------------------------------------
static_assert(wcs::classify(^^std::optional<int>) == marshal_kind::optional_);
static_assert(wcs::classify(^^std::optional<std::string>) ==
              marshal_kind::optional_);
static_assert(wcs::classify(^^std::optional<lockcases::Thing>) ==
              marshal_kind::optional_);
static_assert(wcs::classify(^^std::vector<int>) == marshal_kind::seq_value);
static_assert(wcs::classify(^^std::array<double, 3>) == marshal_kind::seq_value);
static_assert(wcs::classify(^^std::vector<lockcases::Tag>) ==
              marshal_kind::seq_value);
static_assert(wcs::classify(^^std::vector<lockcases::Thing>) ==
              marshal_kind::seq_ref);
// vector<bool> is a bitset; nesting has no wire yet.
static_assert(wcs::classify(^^std::vector<bool>) == marshal_kind::unsupported);
static_assert(wcs::classify(^^std::optional<std::vector<int>>) ==
              marshal_kind::unsupported);
// classify runs AFTER the gate, whose scope-aware oracle already admitted the
// type — so a plain class is a handle (an unregistered one dies loudly at the
// consumer's build on its unresolved raw-name placeholder, never a silent
// void*).
static_assert(wcs::classify(^^lockcases::NotWelded) == marshal_kind::handle);

// --- paired scalar spellings (byte-for-byte agreement) -----------------------
// std::int32_t & co. are using-DECLARATIONS in libstdc++ (unreflectable),
// so the locks spell the fundamentals; scalar_spell keys on size/signedness.
static_assert(streq(wcs::scalar_spell(^^int).c_abi, "std::int32_t"));
static_assert(streq(wcs::scalar_spell(^^int).cs, "int"));
static_assert(streq(wcs::scalar_spell(^^unsigned char).cs, "byte"));
static_assert(streq(wcs::scalar_spell(^^double).c_abi, "double"));
static_assert(streq(wcs::scalar_spell(^^float).cs, "float"));
static_assert(streq(wcs::scalar_spell(^^long long).cs, "long"));
// An enum crosses as its underlying type's pair.
static_assert(streq(wcs::enum_wire_spell(^^lockcases::Tag).cs, "ushort"));

// --- symbol mangling ---------------------------------------------------------
static_assert(wcs::underscore_path(^^lockcases::Thing) ==
              "lockcases_Thing");
static_assert(wcs::qualified_cpp_name(^^lockcases::Thing) ==
              "::lockcases::Thing");

// --- the member-lookup layer (generator <-> shim agreement) ------------------
// named_member(owner, name, k) must invert index_of_named_member for every
// overload — the exact contract the emitted shim relies on.
static_assert(wcs::named_member(^^lockcases::Thing, "get", 0) !=
              wcs::named_member(^^lockcases::Thing, "get", 1));
static_assert(wcs::index_of_named_member(
                  wcs::named_member(^^lockcases::Thing, "get", 1)) == 1);
static_assert(std::meta::parameters_of(
                  wcs::named_member(^^lockcases::Thing, "get", 1)).size() == 1);
static_assert(wcs::index_of_ctor(wcs::ctor_at(^^lockcases::Thing, 2)) == 2);
static_assert(std::meta::identifier_of(
                  wcs::named_field(^^lockcases::Thing, "s")) ==
              std::string_view{"s"});
static_assert(wcs::named_member(^^lockcases, "leaf", 0) == ^^lockcases::leaf);

// --- the operator map + operator lookups --------------------------------------
namespace lockops {
struct [[=welder::weld(welder::lang::cs)]] V {
    int n{0};
    V operator+(const V& o) const { return V{n + o.n}; }
    V operator-() const { return V{-n}; }
    bool operator==(const V& o) const { return n == o.n; }
    int operator[](int i) const { return n + i; }
    auto operator<=>(const V& o) const = default;
};
} // namespace lockops

consteval std::meta::info lockop(std::meta::operators op, bool unary,
                                 std::size_t k = 0) {
    return wcs::named_operator(^^lockops::V, op, unary, k);
}
static_assert(wcs::cs_operator(lockop(std::meta::operators::op_plus, false)).kind ==
              wcs::cs_op_kind::binary);
static_assert(streq(
    wcs::cs_operator(lockop(std::meta::operators::op_plus, false)).cls_name,
    "op_Addition"));
static_assert(wcs::cs_operator(lockop(std::meta::operators::op_minus, true)).kind ==
              wcs::cs_op_kind::unary);
static_assert(streq(
    wcs::cs_operator(lockop(std::meta::operators::op_equals_equals, false)).symbol,
    "=="));
static_assert(
    wcs::cs_operator(lockop(std::meta::operators::op_equals_equals, false)).kind ==
    wcs::cs_op_kind::comparison);
static_assert(
    wcs::cs_operator(lockop(std::meta::operators::op_square_brackets, false)).kind ==
    wcs::cs_op_kind::indexer);
// the lookup inverts its index, like named_member
static_assert(wcs::index_of_operator(
                  lockop(std::meta::operators::op_plus, false)) == 0);
static_assert(wcs::operator_enum_ident(std::meta::operators::op_spaceship) ==
              "op_spaceship");
// spaceship is NOT name-gated (it routes through add_comparisons, never
// add_operator), exactly like the unmapped set
static_assert(wcs::rod::special_method_name(
                  lockop(std::meta::operators::op_spaceship, false)) == nullptr);

// --- parameter identifiers (camelCase + keyword escape) ----------------------
static_assert(wcs::param_ident(
                  std::meta::parameters_of(^^lockcases::leaf)[0], 0) == "v");
// leading underscores strip (both the faithful camelCase and what keeps the
// wrapper's parameter scope disjoint from the generated _-prefixed locals)
static_assert(wcs::param_ident(
                  std::meta::parameters_of(^^lockcases::stripped)[0], 0) ==
              "errSlot");
// The C# name style: PascalCase members, verbatim enumerators.
static_assert(std::string_view{
                  ::welder::name_of<^^lockcases::Thing, welder::lang::cs,
                                    wcs::dotnet, ::welder::ent_kind::class_>()} ==
              "Thing");

// The rod satisfies the full contract (also asserted in rod.hpp; this locks it
// from the test tree so a contract change fails here first).
static_assert(::welder::rod<wcs::rod>);

} // namespace
