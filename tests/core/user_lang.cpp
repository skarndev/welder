// User-defined-language probe (compile-only; must SUCCEED).
//
// Locks the OPEN language value space: a language welder does not ship is
// minted with `welder::user_lang<Slot>` (bits 16–31 of the mask; 0–15 are
// welder's), spelled in annotations exactly like a shipped `lang`, resolved by
// the core (welded_for / excluded_for / member_bound), mixed freely with
// shipped languages in one `weld`, and driven through every welder::welder
// entry point by a rod whose `language` is the user value. Same
// compile-to-pass shape as rod_probe.cpp, whose no-op emission pattern the rod
// below reuses.
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

#include <welder/vocabulary.hpp> // annotation vocabulary (lang, user_lang, weld…)
#include <welder/welder.hpp>     // welder::welder + rod concept + driver

namespace {

// The application-owned identity: minted once, used by the annotations and the
// rod alike (the one-constant convention the docs prescribe).
inline constexpr welder::lang ruby{welder::user_lang<0>};

// Distinct from every shipped language and from the other user slots; sits at
// the documented bit.
static_assert(ruby != welder::lang::py);
static_assert(ruby != welder::lang::lua);
static_assert(welder::user_lang<1> != ruby);
static_assert(welder::user_lang<15> != ruby); // the last valid slot compiles
static_assert(welder::lang_bit(ruby) == 1u << 16);
static_assert(welder::lang_mask(welder::lang::py, ruby) ==
              (welder::lang_bit(welder::lang::py) | 1u << 16));

} // namespace

namespace gems {

// Welded ONLY for the user language.
struct [[=welder::weld(ruby)]] Gem {
    int facets{0};
    [[=welder::mark::exclude(ruby)]] int flaw{0};   // excluded for ruby by name
    [[=welder::mark::exclude]] int internal{0};     // excluded everywhere
    int polish() const { return facets * 2; }
    Gem operator+(const Gem& o) const { return Gem{facets + o.facets}; }
};

// A shipped and a user language mixed in one weld.
struct [[=welder::weld(welder::lang::py, ruby)]] Shared {
    int n{0};
};

[[=welder::weld(ruby)]] inline int carats(int raw) { return raw / 5; }

enum class [[=welder::weld(ruby)]] Cut { Brilliant, Rose };

namespace nested {
struct [[=welder::weld(ruby)]] Facet {
    int angle{0};
};
} // namespace nested

} // namespace gems

// --- the core resolves the user language like any shipped one ---------------

constexpr auto ctx{std::meta::access_context::unchecked()};

static_assert(welder::welded_for(^^gems::Gem, ruby));
static_assert(!welder::welded_for(^^gems::Gem, welder::lang::py));
static_assert(!welder::welded_for(^^gems::Gem, welder::lang::lua));

static_assert(welder::welded_for(^^gems::Shared, ruby));
static_assert(welder::welded_for(^^gems::Shared, welder::lang::py));
static_assert(!welder::welded_for(^^gems::Shared, welder::lang::lua));

static_assert(welder::excluded_for(
    std::meta::nonstatic_data_members_of(^^gems::Gem, ctx)[1], ruby)); // flaw
static_assert(!welder::excluded_for(
    std::meta::nonstatic_data_members_of(^^gems::Gem, ctx)[0], ruby)); // facets
static_assert(welder::excluded_for(
    std::meta::nonstatic_data_members_of(^^gems::Gem, ctx)[2], ruby)); // internal

// --- a rod whose language IS the user value ---------------------------------

namespace {

struct ruby_module {};
struct ruby_class {};
struct ruby_session {};

// Emission primitives are no-ops (rod_probe.cpp exercises those paths); what
// this rod proves is that `language` may be any lang value, not an enumerator.
struct ruby_rod {
    static constexpr welder::lang language{ruby};
    using module_type = ruby_module;
    template <class> using class_handle_type = ruby_class;
    template <class> using enum_handle_type = ruby_class;

    template <class T>
    static constexpr bool has_native_caster = std::is_arithmetic_v<T>;

    static consteval const char* special_method_name(std::meta::info f) {
        if (std::meta::operator_of(f) == std::meta::operators::op_plus &&
            !welder::detail::is_unary_operator(f))
            return "__add";
        return nullptr;
    }

    template <class T, auto Bases, std::size_t... I>
    static ruby_class make_class(module_type&, const char*, const char*,
                                 std::index_sequence<I...>) {
        return {};
    }
    template <class T, auto Ctors, bool HasDefault, bool Aggregate,
              bool Copyable>
    static void add_constructors(auto&) {}
    template <std::meta::info Mem, class Style>
    static void add_field(auto&) {}
    template <auto Fns, class Style>
    static void add_method(auto&) {}
    template <auto Fns, class Style>
    static void add_static_method(auto&) {}
    template <auto Fns>
    static void add_operator(auto&) {}

    template <class E>
    static ruby_class make_enum(module_type&, const char*, const char*) {
        return {};
    }
    template <std::meta::info Enum, class Style>
    static void add_enumerator(auto&) {}
    template <class E>
    static void finish_enum(auto&) {}

    static ruby_session open_module(module_type&) { return {}; }
    static void set_module_doc(module_type&, const char*) {}
    template <auto Fns, class Style>
    static void add_function(module_type&) {}
    template <std::meta::info Var, class Style>
    static void add_variable(module_type&, ruby_session&) {}
    static module_type add_submodule(module_type&, const char*) { return {}; }
    static void close_module(module_type&, ruby_session&) {}
};

static_assert(welder::caster_oracle<ruby_rod>);
static_assert(welder::rod<ruby_rod>);

// Force instantiation of every entry point over the user-language rod; never
// called (the static_asserts above already prove the concept holds).
[[maybe_unused]] void welder_user_lang_exercise_drivers() {
    using weld = welder::welder<ruby_rod>;
    ruby_rod::module_type m{};
    weld::weld_type<gems::Gem>(m);
    weld::weld_type<gems::Shared>(m);
    weld::weld_type<gems::Cut>(m); // enum dispatch
    weld::weld_namespace<^^gems>(m);
    weld::weld_namespace_as_submodule<^^gems::nested>(m);
    weld::weld_module<^^gems>(
        m, [](ruby_rod::module_type&) {},
        [](ruby_rod::module_type&) {});
}

} // namespace