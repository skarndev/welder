// Backend-abstraction probe (backend-agnostic; must SUCCEED to compile).
//
// A minimal, self-contained backend that satisfies `welder::backend` using ONLY
// welder's core headers — no pybind11, no Python, no framework at all. It is
// driven through all three generic drivers (bind_type / bind_namespace_driver /
// build_module_driver). The point is architectural, not behavioral: if this
// compiles, the core traversal/resolution/bindability machinery is genuinely
// decoupled from any concrete backend, so a new backend (nanobind, lua, ...) only
// has to implement the emission primitives below. It is a regression guard against
// re-coupling the core to pybind11.
//
// Compiled by the `compile.backend_probe` CTest (a plain build target, no
// WILL_FAIL). Every emission primitive is a no-op: we exercise *instantiation* of
// the drivers over the interface, not any real registration.
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

#include <welder/welder.hpp>  // vocabulary + reflection + doc (header-only core)
#include <welder/backend.hpp> // the backend concept + generic driver

namespace {

// The backend's opaque handles. A real backend's would be framework types (e.g.
// pybind11's py::module_ / py::class_); here they carry nothing — the drivers only
// pass them around, never inspect them.
struct probe_module {};
struct probe_class {};
struct probe_session {};

// A backend is a stateless policy struct. Every member below is required by
// welder::backend (see <welder/backend.hpp>); the bodies do nothing.
struct probe_backend {
    static constexpr welder::lang language{welder::lang::py};
    using module_type = probe_module;

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
    static void add_default_ctor(auto&) {}
    template <std::meta::info Ctor>
    static void add_constructor(auto&) {}
    template <class T>
    static void add_aggregate_constructor(auto&) {}
    template <std::meta::info Mem>
    static void add_field(auto&) {}
    template <std::meta::info Fn>
    static void add_method(auto&) {}
    template <std::meta::info Fn>
    static void add_static_method(auto&) {}
    template <std::meta::info Fn>
    static void add_operator(auto&) {}

    // --- namespace / module binding -----------------------------------------
    static probe_session open_module(module_type&) { return {}; }
    static void set_module_doc(module_type&, const char*) {}
    template <std::meta::info Fn>
    static void add_function(module_type&) {}
    template <std::meta::info Var>
    static void add_variable(module_type&, probe_session&) {}
    static module_type add_submodule(module_type&, const char*) { return {}; }
    static void close_module(module_type&, probe_session&) {}
};

// The interface is satisfied purely statically — the core never mentions this type.
static_assert(welder::caster_oracle<probe_backend>);
static_assert(welder::backend<probe_backend>);

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

namespace nested {
struct [[=welder::weld(welder::lang::py)]] Inner {
    int value{0};
};
} // namespace nested

} // namespace probe

// Force instantiation of all three drivers over the probe backend. Never called;
// its purpose is to make the compiler type-check the driver body against the
// interface. The static_asserts above already prove the concept holds.
[[maybe_unused]] void welder_probe_exercise_drivers() {
    probe_backend::module_type m{};
    welder::detail::bind_type<probe_backend, probe::Widget>(m, nullptr);
    welder::detail::bind_type<probe_backend, probe::Point>(m, nullptr);
    welder::detail::bind_namespace_driver<probe_backend, ^^probe>(m);
    welder::detail::build_module_driver<probe_backend, ^^probe>(
        m, [](probe_backend::module_type&) {},
        [](probe_backend::module_type&) {});
}
