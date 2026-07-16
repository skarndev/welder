// Class-NESTED type resolution + the gate's registration oracle (compile-only).
// The runtime behavior is covered by test_nested.py / nested_spec.lua (all four
// runtime rods) and the luacats golden; these static_asserts lock the predicates:
//
//   - welder::is_nested_type: class-scoped vs namespace-scoped;
//   - counts_as_registered (both shipped resolutions): a nested type counts
//     exactly when the SWEEP registers it — it resolves like a member (the
//     OUTER's policy + its own exclude/include/only marks, access admitted),
//     never via a weld of its own; the outer must itself count (recursively);
//     unnameable/incomplete member types never count;
//   - the welded_for bypass: a nested type carrying its own weld counts under
//     the stitch oracle even when marked out of the sweep — the escape hatch
//     for "exclude from the outer, weld manually (flat)";
//   - greedy_resolution: the same member rules over an UNMARKED outer, with the
//     WeldProtected knob arbitrating protected nested types.
#include <welder/vocabulary.hpp>
#include <welder/carriage.hpp>

#include <string_view>

namespace nt {

struct [[=welder::weld(welder::lang::py)]] Outer {
    struct Pub {
        int a{};
    };
    struct [[=welder::mark::exclude]] Out {
        int b{};
    };
    struct [[=welder::mark::exclude(welder::lang::py)]] NoPy {
        int c{};
    };
    // its own weld: not swept differently, but the stitch oracle trusts a manual
    // (flat) weld_type<Outer::Escape> — the exclude+weld combo.
    struct [[=welder::weld(welder::lang::py), =welder::mark::exclude]] Escape {
        int d{};
    };
    struct Fwd; // incomplete: the sweep skips it, the oracle rejects it
    struct Deep {
        struct Deeper {
            int e{};
        };
    };

  protected:
    struct Prot {
        int p{};
    };

  private:
    struct Priv {
        int q{};
    };
};

struct [[=welder::weld(welder::lang::py), =welder::policy::weld_protected]]
ProtOuter {
  protected:
    struct Prot {
        int p{};
    };
};

struct [[=welder::weld(welder::lang::py), =welder::policy::opt_in]] Choosy {
    struct [[=welder::mark::include]] In {
        int i{};
    };
    struct NotIn {
        int j{};
    };
};

struct Unwelded { // no weld: its nested types never count under stitch
    struct Inner {
        int k{};
    };
};

} // namespace nt

namespace {

using welder::lang;
namespace carriages = welder::carriages;
using stitch = carriages::marker_resolution;
using tack = carriages::greedy_resolution<>;
using tack_prot = carriages::greedy_resolution<true>;

// Protected/private nested types cannot be *named* from here (access-checked at
// lookup), so reach any member type through members_of with the unchecked context.
consteval std::meta::info member_named(std::meta::info cls, std::string_view name) {
    for (auto m : std::meta::members_of(
             cls, std::meta::access_context::unchecked()))
        if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == name)
            return m;
    return std::meta::info{};
}

// --- is_nested_type -----------------------------------------------------------
static_assert(welder::is_nested_type(^^nt::Outer::Pub));
static_assert(welder::is_nested_type(^^nt::Outer::Deep::Deeper));
static_assert(!welder::is_nested_type(^^nt::Outer));

// --- stitch: the sweep's rules, mirrored by the oracle -------------------------
static_assert(stitch::counts_as_registered(^^nt::Outer::Pub, lang::py));
static_assert(!stitch::counts_as_registered(^^nt::Outer::Pub, lang::lua)); // outer not welded for lua
static_assert(!stitch::counts_as_registered(member_named(^^nt::Outer, "Out"),
                                            lang::py)); // excluded
static_assert(!stitch::counts_as_registered(member_named(^^nt::Outer, "NoPy"),
                                            lang::py)); // lang-scoped exclude
static_assert(!stitch::counts_as_registered(member_named(^^nt::Outer, "Fwd"),
                                            lang::py)); // incomplete
static_assert(stitch::counts_as_registered(^^nt::Outer::Deep::Deeper,
                                           lang::py)); // recurses through Deep
static_assert(!stitch::counts_as_registered(^^nt::Unwelded::Inner,
                                            lang::py)); // outer never registers

// the exclude+weld combo: out of the sweep, but the manual (flat) weld counts.
static_assert(stitch::counts_as_registered(member_named(^^nt::Outer, "Escape"),
                                           lang::py));

// access: protected only under the outer's weld_protected; private never.
static_assert(!stitch::counts_as_registered(member_named(^^nt::Outer, "Prot"),
                                            lang::py));
static_assert(!stitch::counts_as_registered(member_named(^^nt::Outer, "Priv"),
                                            lang::py));
static_assert(stitch::counts_as_registered(member_named(^^nt::ProtOuter, "Prot"),
                                           lang::py));

// opt_in outer: a nested type binds only when explicitly included.
static_assert(stitch::counts_as_registered(^^nt::Choosy::In, lang::py));
static_assert(!stitch::counts_as_registered(^^nt::Choosy::NotIn, lang::py));

// --- tack: same member rules over an unmarked outer -----------------------------
static_assert(tack::counts_as_registered(^^nt::Unwelded::Inner, lang::py));
static_assert(!tack::counts_as_registered(member_named(^^nt::Outer, "Out"),
                                          lang::py)); // marks still prune
static_assert(!tack::counts_as_registered(member_named(^^nt::Outer, "Priv"),
                                          lang::py)); // private never, any knob
static_assert(!tack::counts_as_registered(member_named(^^nt::Outer, "Prot"),
                                          lang::py)); // knob off, no annotation
static_assert(tack_prot::counts_as_registered(member_named(^^nt::Outer, "Prot"),
                                              lang::py)); // whole-pass knob

} // namespace