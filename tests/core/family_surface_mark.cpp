// The family_surface mark (compile-only; must SUCCEED).
//
// Locks the storage + query contract of `[[=welder::mark::family_surface]]`:
// the TYPE mark a welded base carries to opt its family (two or more welded
// classes deriving it) into a rod-synthesized version-agnostic surface.
// welder core only stores and answers `family_surface_for`; the surface
// itself is the honoring rod's contract (the C# rod's dispatch members are
// the first). The semantics locked here:
//   - bare mark  -> covers every language, shipped and user-minted alike;
//   - scoped mark -> covers exactly the named languages;
//   - repeated marks union their languages;
//   - an unmarked base answers false everywhere — the opt-in default a rod
//     must respect by synthesizing nothing.
#include <welder/vocabulary.hpp>
#include <welder/reflect.hpp> // family_surface_for

namespace {

inline constexpr welder::lang ruby{welder::user_lang<0>};

struct [[=welder::weld]] PlainBase {};

struct
[[=welder::weld]]
[[=welder::mark::family_surface]]
BareBase {};

struct
[[=welder::weld]]
[[=welder::mark::family_surface(welder::lang::py)]]
PyBase {};

struct
[[=welder::weld]]
[[=welder::mark::family_surface(welder::lang::py)]]
[[=welder::mark::family_surface(ruby)]]
UnionBase {};

// Unmarked: the opt-in default — false for every language.
static_assert(!welder::family_surface_for(^^PlainBase, welder::lang::py));
static_assert(!welder::family_surface_for(^^PlainBase, welder::lang::lua));
static_assert(!welder::family_surface_for(^^PlainBase, ruby));

// Bare: every language, user-minted ones included.
static_assert(welder::family_surface_for(^^BareBase, welder::lang::py));
static_assert(welder::family_surface_for(^^BareBase, welder::lang::lua));
static_assert(welder::family_surface_for(^^BareBase, ruby));

// Scoped: exactly the named languages.
static_assert(welder::family_surface_for(^^PyBase, welder::lang::py));
static_assert(!welder::family_surface_for(^^PyBase, welder::lang::lua));
static_assert(!welder::family_surface_for(^^PyBase, ruby));

// Repeated marks union.
static_assert(welder::family_surface_for(^^UnionBase, welder::lang::py));
static_assert(welder::family_surface_for(^^UnionBase, ruby));
static_assert(!welder::family_surface_for(^^UnionBase, welder::lang::lua));

} // namespace
