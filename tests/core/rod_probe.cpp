// Rod-abstraction probe (backend-agnostic; must SUCCEED to compile).
//
// A minimal, self-contained rod that satisfies `welder::rod` using ONLY
// welder's core headers — no pybind11, no Python, no framework at all. It is
// driven through every welder::welder entry point (weld_type / weld_namespace /
// weld_namespace_as_submodule / weld_module). The point is architectural, not
// behavioral: if this
// compiles, the core traversal/resolution/bindability machinery is genuinely
// decoupled from any concrete rod, so a new rod (nanobind, lua, ...) only
// has to implement the emission primitives below. It is a regression guard against
// re-coupling the core to pybind11.
//
// Compiled by the `compile.rod_probe` CTest (a plain build target, no
// WILL_FAIL). Every emission primitive is a no-op: we exercise *instantiation* of
// the drivers over the interface, not any real registration.
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/vocabulary.hpp> // annotation vocabulary (header-only form)
#include <welder/welder.hpp>     // welder::welder + rod concept + driver

namespace {

// The backend's opaque handles. A real backend's would be framework types (e.g.
// pybind11's py::module_ / py::class_); here they carry nothing — the drivers only
// pass them around, never inspect them.
struct probe_module {};
struct probe_class {};
struct probe_session {};

// A rod is a stateless policy struct. Every member below is required by
// welder::rod (see <welder/welder.hpp>); the bodies do nothing.
struct probe_rod {
    static constexpr welder::lang language{welder::lang::py};
    using module_type = probe_module;

    // The class / enum handle types make_class / make_enum yield (here the same opaque
    // handle for both). Named as associated types so welder::rod can shape-check the
    // per-handle hooks against them without deducing them from a factory call.
    template <class> using class_handle_type = probe_class;
    template <class> using enum_handle_type = probe_class;

    // caster_oracle leaf: pretend the "native" convertible types are the
    // arithmetic scalars (as if the backend shipped casters for them). A welded
    // class member is not native, so bindability then defers to `welded_for`.
    template <class T>
    static constexpr bool has_native_caster = std::is_arithmetic_v<T>;

    // Expose only a binary operator+ (as "__add__"); everything else is unmapped.
    // Enough to exercise the driver's add_operator path.
    static consteval const char* special_method_name(std::meta::info f) {
        if (std::meta::operator_of(f) == std::meta::operators::op_plus &&
            !welder::detail::is_unary_operator(f))
            return "__add__";
        return nullptr;
    }

    // --- class binding ------------------------------------------------------
    template <class T, auto Bases, std::size_t... I>
    static probe_class make_class(module_type&, const char*, const char*,
                                  std::index_sequence<I...>) {
        return {};
    }
    // Constructors arrive as ONE call: the participating constructor reflections
    // (an array NTTP) plus the carriage-computed default/aggregate flags.
    template <class T, auto Ctors, bool HasDefault, bool Aggregate>
    static void add_constructors(auto&) {}
    // The name-producing primitives take a trailing name-style parameter (the
    // driver threads welder::welder's Style through); a real rod resolves its own
    // name via welder::name_of<…, Style, …>. The probe ignores it. Callables
    // arrive as whole overload GROUPS (`auto Fns`, a std::array<std::meta::info,
    // N> sharing one target name — resolve it from Fns[0]).
    template <std::meta::info Mem, class Style>
    static void add_field(auto&) {}
    template <auto Fns, class Style>
    static void add_method(auto&) {}
    template <auto Fns, class Style>
    static void add_static_method(auto&) {}
    template <auto Fns>
    static void add_operator(auto&) {}

    // --- enum binding -------------------------------------------------------
    template <class E>
    static probe_class make_enum(module_type&, const char*, const char*) {
        return {};
    }
    template <std::meta::info Enum, class Style>
    static void add_enumerator(auto&) {}
    template <class E>
    static void finish_enum(auto&) {}

    // --- namespace / module binding -----------------------------------------
    static probe_session open_module(module_type&) { return {}; }
    static void set_module_doc(module_type&, const char*) {}
    template <auto Fns, class Style>
    static void add_function(module_type&) {}
    template <std::meta::info Var, class Style>
    static void add_variable(module_type&, probe_session&) {}
    static module_type add_submodule(module_type&, const char*) { return {}; }
    static void close_module(module_type&, probe_session&) {}
};

// The interface is satisfied purely statically — the core never mentions this type.
static_assert(welder::caster_oracle<probe_rod>);
static_assert(welder::rod<probe_rod>);

} // namespace

// A namespace whose contents exercise every driver branch: a class with a
// default ctor, a value ctor, a method, a static method and a mapped operator; a
// baseless aggregate (synthesized field ctor); a free function; a const variable
// (value snapshot) and a mutable one (live property); and a nested namespace that
// holds bound content (becomes a submodule).
namespace probe {

struct [[=welder::weld(welder::lang::py)]] Widget {
    int width{0};
    int height{0};
    Widget() = default;
    Widget(int w, int h) : width{w}, height{h} {}
    int area() const { return width * height; }
    static Widget square(int side) { return Widget{side, side}; }
    Widget operator+(const Widget& o) const {
        return Widget{width + o.width, height + o.height};
    }
};

struct [[=welder::weld(welder::lang::py)]] Point { // aggregate
    double x{0};
    double y{0};
};

[[=welder::weld(welder::lang::py)]] inline int gain(int a, int b) { return a + b; }

[[=welder::weld(welder::lang::py)]] inline constexpr int kMax{100}; // const snapshot
[[=welder::weld(welder::lang::py)]] inline int counter{0};          // live property

enum class [[=welder::weld(welder::lang::py)]] Mode { Fast, Slow };

namespace nested {
struct [[=welder::weld(welder::lang::py)]] Inner {
    int value{0};
};
} // namespace nested

} // namespace probe

// Force instantiation of every welder::welder entry point over the probe
// backend. Never called; its purpose is to make the compiler type-check the
// driver body against the interface. The static_asserts above already prove the
// concept holds.
[[maybe_unused]] void welder_probe_exercise_drivers() {
    using weld = welder::welder<probe_rod>;
    probe_rod::module_type m{};
    weld::weld_type<probe::Widget>(m);
    weld::weld_type<probe::Point>(m);
    weld::weld_type<probe::Mode>(m); // enum dispatch
    weld::weld_namespace<^^probe>(m);
    weld::weld_namespace_as_submodule<^^probe::nested>(m);
    weld::weld_module<^^probe>(
        m, [](probe_rod::module_type&) {},
        [](probe_rod::module_type&) {});
}
