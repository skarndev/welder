// Property naming + pairing semantics (compile-only; must SUCCEED to compile).
//
// The runtime property behavior is asserted per rod (test_properties.py /
// properties_spec.lua); locked here are the consteval pieces those tests
// observe only indirectly: case detection, the accessor-word strip (both the
// raw word list — the PAIRING key — and the styled-name derivation), the
// per-style property_bound_name resolution (explicit-name verbatim vs
// styled-then-stripped), and the opt_in implication in member_bound. Same
// compile-to-pass shape as the other core tests; needs only the compiler.
#include <string_view>

#include <welder/vocabulary.hpp> // annotation vocabulary (weld / getter / setter)
#include <welder/bind_traits.hpp> // property_key / property_entries / property_bound_name
#include <welder/carriage.hpp>    // marker_resolution (the shipped stitch rules)

using namespace std::string_view_literals;
namespace nm = welder::naming;
using welder::accessor_role;
using welder::lang;

// --- case detection: the inverse guess the accessor strip re-joins with ------
static_assert(nm::detect_case("max_size"sv) == nm::case_kind::snake);
static_assert(nm::detect_case("MAX_SIZE"sv) == nm::case_kind::screaming_snake);
static_assert(nm::detect_case("maxSize"sv) == nm::case_kind::camel);
static_assert(nm::detect_case("MaxSize"sv) == nm::case_kind::pascal);
static_assert(nm::detect_case("max-size"sv) == nm::case_kind::kebab);
static_assert(nm::detect_case("maxsize"sv) == nm::case_kind::snake); // moot: all joins agree

// --- the accessor-word strip: property words (the pairing key) ---------------
consteval bool prop_words(std::string_view id,
                          std::initializer_list<std::string_view> expect) {
    auto ws{nm::accessor_property_words(id)};
    if (ws.size() != expect.size())
        return false;
    std::size_t i{0};
    for (auto e : expect)
        if (ws[i++] != e)
            return false;
    return true;
}
static_assert(prop_words("get_max_size"sv, {"max"sv, "size"sv}));
static_assert(prop_words("setMaxSize"sv, {"max"sv, "size"sv}));
static_assert(prop_words("GetMaxSize"sv, {"max"sv, "size"sv}));
static_assert(prop_words("GET_MAX_SIZE"sv, {"max"sv, "size"sv}));
static_assert(prop_words("getURL"sv, {"url"sv}));          // acronym run
static_assert(prop_words("radius"sv, {"radius"sv}));       // no accessor word
static_assert(prop_words("get"sv, {"get"sv}));             // nothing would remain
static_assert(prop_words("is_ready"sv, {"is"sv, "ready"sv})); // predicates keep is_

// --- the styled-name strip: preserve the styled spelling's own convention ----
static_assert(nm::strip_accessor_word("get_max_size"sv) == "max_size");
static_assert(nm::strip_accessor_word("getMaxSize"sv) == "maxSize");
static_assert(nm::strip_accessor_word("GetMaxSize"sv) == "MaxSize");
static_assert(nm::strip_accessor_word("GET_MAX_SIZE"sv) == "MAX_SIZE");
static_assert(nm::strip_accessor_word("radius"sv) == "radius");
static_assert(nm::strip_accessor_word("_get_hidden"sv) == "_hidden"); // fixup underscores survive

// --- pairing + resolution over a real welded type ----------------------------
namespace props_core {

struct [[=welder::weld(welder::lang::py)]] Mixed {
    Mixed() = default;
    // Mixed conventions pair on the case-normalized words: the snake getter and
    // the pascal setter are ONE property, spelled by the getter.
    [[=welder::getter]] int get_max_speed() const { return v_; }
    [[=welder::setter]] void SetMaxSpeed(int v) { v_ = v; }
    // A lone getter is a read-only property.
    [[=welder::getter]] int getCount() const { return 0; }
    // An explicit name pairs verbatim, whatever the identifiers.
    [[=welder::getter("level")]] double raw() const { return 0.0; }
    [[=welder::setter("level")]] void assign(double) {}

  private:
    int v_{0};
};

// opt_in: the accessor mark IS the opt-in; an unmarked sibling stays out.
struct [[=welder::weld(welder::lang::py), =welder::policy::opt_in]] Chosen {
    Chosen() = default;
    [[=welder::getter]] int get_code() const { return 0; }
    int stray() const { return 0; }
};

} // namespace props_core

using res = welder::carriages::marker_resolution;

// The resolved property set: three entries, in getter declaration order.
static_assert(welder::detail::property_entries<res>(^^props_core::Mixed,
                                                    lang::py).size() == 3);
consteval bool mixed_pairs_ok() {
    auto ps{welder::detail::property_entries<res>(^^props_core::Mixed, lang::py)};
    // [0] max_speed: paired across conventions.
    if (std::meta::identifier_of(ps[0].getter) != "get_max_speed"sv ||
        ps[0].setter == std::meta::info{} ||
        std::meta::identifier_of(ps[0].setter) != "SetMaxSpeed"sv)
        return false;
    // [1] count: read-only.
    if (std::meta::identifier_of(ps[1].getter) != "getCount"sv ||
        ps[1].setter != std::meta::info{})
        return false;
    // [2] level: explicit names pair verbatim.
    return std::meta::identifier_of(ps[2].getter) == "raw"sv &&
           ps[2].setter != std::meta::info{} &&
           std::meta::identifier_of(ps[2].setter) == "assign"sv;
}
static_assert(mixed_pairs_ok());

// The pairing key normalizes case; the explicit name replaces the identifier.
static_assert(welder::detail::property_key(
                  ^^props_core::Mixed::get_max_speed, accessor_role::getter,
                  lang::py) ==
              welder::detail::property_key(^^props_core::Mixed::SetMaxSpeed,
                                           accessor_role::setter, lang::py));
static_assert(prop_words("level"sv, {"level"sv}));

// --- property_bound_name: explicit verbatim, else styled-then-stripped -------
consteval std::string_view bound_name(const char* s) { return s; }
// naming::none preserves the source convention of the remainder.
static_assert(bound_name(welder::detail::property_bound_name<
                             ^^props_core::Mixed::get_max_speed, lang::py,
                             nm::none>()) == "max_speed"sv);
static_assert(bound_name(welder::detail::property_bound_name<
                             ^^props_core::Mixed::getCount, lang::py,
                             nm::none>()) == "count"sv);
// A styled welder reshapes first, then strips in the styled convention.
static_assert(bound_name(welder::detail::property_bound_name<
                             ^^props_core::Mixed::getCount, lang::py,
                             nm::snake_case>()) == "count"sv);
static_assert(bound_name(welder::detail::property_bound_name<
                             ^^props_core::Mixed::get_max_speed, lang::py,
                             nm::pascal_case>()) == "MaxSpeed"sv);
// The explicit name is verbatim — it never flows through the style.
static_assert(bound_name(welder::detail::property_bound_name<
                             ^^props_core::Mixed::raw, lang::py,
                             nm::pascal_case>()) == "level"sv);

// --- opt_in implication in member_bound --------------------------------------
static_assert(welder::member_bound(^^props_core::Chosen::get_code, lang::py,
                                   welder::policy_kind::opt_in));
static_assert(!welder::member_bound(^^props_core::Chosen::stray, lang::py,
                                    welder::policy_kind::opt_in));
// The implication is scoped: the mark covers py only after scoping, so a
// lang-scoped accessor does not opt the member in elsewhere.
namespace props_core {
struct [[=welder::weld(welder::lang::py, welder::lang::lua),
         =welder::policy::opt_in]] Scoped {
    Scoped() = default;
    [[=welder::getter(welder::lang::py)]] int get_x() const { return 0; }
};
} // namespace props_core
static_assert(welder::member_bound(^^props_core::Scoped::get_x, lang::py,
                                   welder::policy_kind::opt_in));
static_assert(!welder::member_bound(^^props_core::Scoped::get_x, lang::lua,
                                    welder::policy_kind::opt_in));

int main() {}