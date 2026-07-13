#pragma once

// welder is C++26-and-newer only and is built on P2996 reflection. On the one
// toolchain that implements it today, gcc-16, that means `-std=c++26 -freflection`,
// which together define __cpp_impl_reflection. We check the capability here — the
// first header any welder TU pulls in (via <welder/vocabulary.hpp>) — so a
// misconfigured target fails with this one clear message instead of a wall of
// syntax errors deeper in the machinery. welder deliberately does NOT force the C++
// standard onto your target from CMake (no INTERFACE cxx_std_26): the language level
// is your project's dial to set. (Broaden the macro check as other compilers land
// P2996.)
#if !defined(__cpp_impl_reflection)
#  error "welder requires C++26 reflection. On gcc-16 build this target with " \
         "`-std=c++26 -freflection` (set CMAKE_CXX_STANDARD to 26 or newer). " \
         "See the welder \"Getting started\" guide."
#endif

/** @file
    Target-language vocabulary — the set of languages welder can bind to.

    @note Intentionally free of any standard-library include. welder ships
    header-only today, but the vocabulary is kept std-include-free so it can be
    re-exported by a future `welder` C++20 module without a std-leak: std headers
    in a module unit's purview/GMF leak into every importer (conflicting with
    their textual std includes on gcc-16). See the "Header-only for now" page in
    the docs for why modularization is deferred.
*/

namespace welder::inline v0 {

/** The target languages welder ships rods for — but not the whole value space.

    A language is a **bit index** into the `unsigned` mask the annotation
    vocabulary stores (`lang_bit` / `lang_mask` in `<welder/annotations.hpp>`),
    and the value space is deliberately **open**: these enumerators are the
    languages welder ships, yet any in-range `welder::lang` value is a valid
    language. Indices **0–15 are reserved for welder**; an out-of-tree rod
    binding a language welder doesn't ship mints its identity from the user
    range (16–31) with @ref welder::user_lang — see the "Extending welder"
    guide page.
*/
enum class lang : unsigned char {
    py,   /**< Python (via the pybind11 and nanobind backends). */
    lua,  /**< Lua (via the sol2 and LuaBridge3 backends). */
};

/** A **user-defined language**: the @a Slot-th identity from the mask's user
    range (bit `16 + Slot`) — for binding a language welder does not ship,
    without touching welder. Constrained so it can never collide with a
    welder-shipped language and never exceeds the mask width.

    Mint it **once, in one place** — the application that instantiates the rod
    — and spell both the annotations and the rod's language from that single
    constant, so the two can never disagree:
    @code
    inline constexpr welder::lang ruby{welder::user_lang<0>};

    struct [[=welder::weld(ruby)]] Gem { ... };

    rods_ruby::rod<ruby>   // a third-party rod's injectable language parameter
    @endcode
    Two third-party rods that both default to the same slot are re-pointed the
    same way — pass each a distinct `user_lang` at the instantiation site.

    @tparam Slot the user-range slot, 0–15.
*/
template <unsigned char Slot>
    requires (Slot < 16)
inline constexpr lang user_lang{static_cast<lang>(16 + Slot)};

} // namespace welder
