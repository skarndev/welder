// Trampoline slot semantics (compile-only): what welder counts as an overridable
// virtual slot and what its generator renders for the hard signature shapes. The
// runtime behavior is covered by tests/python/test_trampoline.py (hand-written) and
// test_gen_trampolines.py (generated); these static_asserts lock the *reflection*
// semantics without needing a Python backend:
//
//   - a COVARIANT override is ONE slot, kept with the most-derived (narrowed) return;
//   - an OVERLOADED virtual is one slot per overload, each selectable by function
//     type via virtual_slot (a textual ^^Base::name would be ill-formed);
//   - PROTECTED virtuals (NVI hooks) are slots; PRIVATE declarations are not, and a
//     private redeclaration *withdraws* an inherited public slot;
//   - an override that strengthens `noexcept` is still the same slot;
//   - a C-VARIADIC virtual (irreproducible: P2996 has no ellipsis query) makes the
//     generator emit a static_assert, not silently-uncovering generated code.
//
// (trampoline.hpp / trampolines/document.hpp are rod-agnostic reflection + text —
// no pybind11/nanobind needed — so this lives under core/ like doc_styles.cpp.)
#include <welder/vocabulary.hpp>
#include <welder/rods/python/trampoline.hpp>
#include <welder/rods/python/trampolines/document.hpp>

#include <string>
#include <string_view>

namespace wrp = welder::rods::python;
namespace wrt = welder::rods::trampolines;

// --- covariant returns: one vtable slot, most-derived declaration kept ---------

namespace ts {

struct Plant {
    virtual ~Plant() = default;
    virtual Plant* parent() const { return nullptr; }
};
struct Tree : Plant {
    Tree* parent() const override { return nullptr; }  // covariant
};

static_assert(wrp::virtual_slot_count(^^Plant) == 1);
static_assert(wrp::virtual_slot_count(^^Tree) == 1,
              "covariant override must dedup to ONE slot, not two");
static_assert(std::meta::return_type_of(wrp::overridable_virtuals(^^Tree)[0]) ==
                  ^^Tree*,
              "the kept slot carries the most-derived (narrowed) return type");

// A trampoline redeclaring the narrowed signature covers the slot.
struct PyTree : Tree {
    Tree* parent() const override { return nullptr; }
};
static_assert(wrp::trampoline_covers(^^Tree, ^^PyTree));

// --- overloads: one slot per overload, selected by function type ---------------

struct Robot {
    virtual ~Robot() = default;
    virtual std::string send(int) const { return {}; }
    virtual std::string send(const std::string&) const { return {}; }
};

static_assert(wrp::virtual_slot_count(^^Robot) == 2);
static_assert(wrp::virtual_slot(^^Robot, "send", ^^std::string(int) const) !=
                  wrp::virtual_slot(^^Robot, "send",
                                    ^^std::string(const std::string&) const),
              "virtual_slot must distinguish the overloads by function type");
static_assert(std::meta::is_virtual(
    wrp::virtual_slot(^^Robot, "send", ^^std::string(int) const)));

// virtual_slot sees inherited slots too (it searches overridable_virtuals).
struct Droid : Robot {};
static_assert(wrp::virtual_slot(^^Droid, "send", ^^std::string(int) const) ==
              wrp::virtual_slot(^^Robot, "send", ^^std::string(int) const));

// --- access: protected is a slot, private is not, privatizing withdraws --------

struct Nvi {
    virtual ~Nvi() = default;
    std::string religion() const { return rite(); }  // the NVI caller

  protected:
    virtual std::string rite() const { return "hum"; }  // hook: a real slot

  private:
    virtual int hidden() const { return 0; }  // unroutable: base call inaccessible
};
static_assert(wrp::virtual_slot_count(^^Nvi) == 1,
              "protected hook in, private virtual out");

struct NviShut : Nvi {
  private:
    std::string rite() const override { return "silence"; }  // privatizes the slot
};
static_assert(wrp::virtual_slot_count(^^NviShut) == 0,
              "a private redeclaration withdraws the inherited slot");

// --- noexcept strengthening: still the same slot -------------------------------

struct Soft {
    virtual ~Soft() = default;
    virtual int op() { return 1; }
};
struct Hard : Soft {
    int op() noexcept override { return 2; }  // strengthened exception spec
};
static_assert(wrp::virtual_slot_count(^^Hard) == 1,
              "a noexcept-strengthening override is the same slot");
static_assert(std::meta::is_noexcept(wrp::overridable_virtuals(^^Hard)[0]),
              "…kept with the most-derived (noexcept) declaration");

// --- the generator's rendering of the hard shapes -------------------------------

consteval bool contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}
consteval std::size_t count(std::string_view hay, std::string_view needle) {
    std::size_t n{0};
    for (auto pos{hay.find(needle)}; pos != std::string_view::npos;
         pos = hay.find(needle, pos + needle.size()))
        ++n;
    return n;
}

// Overloads: one slot-reflection override per overload, none keyed on the (ill-
// formed) textual ^^Robot::send.
static_assert(count(wrt::render_trampoline(^^Robot), "WELDER_PY_OVERRIDE_AS(") == 2);
static_assert(!contains(wrt::render_trampoline(^^Robot), "^^ts::Robot::send"));

// Covariant: exactly one override rendered (two same-name declarations differing
// only in return type would not compile).
static_assert(count(wrt::render_trampoline(^^Tree), "override {") == 1);

// C-variadic: irreproducible, so the generator must emit a static_assert (a clear
// build error), not a trampoline that silently fails coverage.
struct Printer {
    virtual ~Printer() = default;
    virtual void log(const char* /*fmt*/, ...) {}
};
static_assert(wrt::is_c_variadic(wrp::overridable_virtuals(^^Printer)[0]));
static_assert(contains(wrt::render_trampoline(^^Printer), "static_assert(false"));
static_assert(contains(wrt::render_trampoline(^^Printer), "bind_flat"));

// --- rendering a class-template instantiation via its alias --------------------

// `Ring<int>` has no identifier; its namespace-scope alias is the C++ spelling the
// generated text derives from — base clause, trampoline_for key, and the slot
// re-derivations all name ::ts::IntRing.
template <class T>
struct Ring {
    virtual ~Ring() = default;
    virtual T next() { return T{}; }
};
using IntRing = Ring<int>;

static_assert(wrp::virtual_slot_count(^^IntRing) == 1,
              "the reflection layer accepts an alias (dealiased at entry)");
static_assert(contains(wrt::render_trampoline(^^IntRing),
                       "struct ts_IntRing_trampoline : ::ts::IntRing {"));
static_assert(contains(wrt::render_trampoline(^^IntRing),
                       "overridable_virtuals(^^::ts::IntRing)"));
static_assert(contains(wrt::render_registration(^^IntRing),
                       "trampoline_for<::ts::IntRing>"));

} // namespace ts