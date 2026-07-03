#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

// The STL templates named in the element-wise wrapper table below must be
// complete/visible so `^^std::vector` & friends resolve.
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include <welder/bind_traits.hpp> // param_types (for signature asserts)
#include <welder/reflect.hpp>     // welded_for

/** @file
    Backend-agnostic bindability ("can the target language represent this type?").

    Before welder emits a binding for any surface (a data member, a parameter, a
    return type, a namespace variable), it checks that the backend can actually
    turn that C++ type into a *meaningful* target value. Binding one it cannot is
    never useful: the attribute would be unusable AND generated stubs would
    reference an unimportable type. So welder makes it a hard compile error (see
    assert_bindable()), whose remedy is to weld the type, give it a backend type
    converter, or `mark::exclude` the member.

    Only ONE thing is backend-specific here: the leaf test — "does the backend
    natively convert this scalar/string/user-registered type, or does it need
    welder to register a class/enum for it?". A backend supplies that via the
    #caster_oracle concept; the STL-wrapper recursion below is shared, so a new
    backend inherits container/optional/variant/smart-pointer handling for free.
*/

namespace welder {

/** The one bindability fact a backend must provide: can it natively convert a
    type *without* welder registering it?

    `has_native_caster<T>` is `true` for a native scalar/string/STL type, or one
    the user gave a bespoke backend converter; `false` means @a T is a
    program-defined class/enum the backend can only handle once welder has
    registered it (so welder then requires @a T to be welded).

    @tparam B the backend type.
*/
template <class B>
concept caster_oracle = requires {
    { B::template has_native_caster<int> } -> std::convertible_to<bool>;
    { B::template has_native_caster<double> } -> std::convertible_to<bool>;
};

namespace detail {

/** Forward declaration: bindable() recurses through a container's element types. */
template <caster_oracle B, class T, lang L>
consteval bool bindable();

/** One row of the element-wise STL-wrapper table.

    The STL templates whose target conversion is *element-wise* — the standard
    containers, `std::optional`, `pair`/`tuple`/`variant` and the smart-pointer
    holders — each paired with how many of its *leading* template arguments are
    value-bearing (recursed): a sequence/optional/holder exposes 1, a map or pair
    exposes 2 (key and value). A count of 0 is the sentinel for "all arguments"
    (tuple, variant). Everything after those leading args — allocators,
    comparators, hashers, deleters, the `std::array` extent — is infrastructure the
    backend never converts, so it is ignored.

    Reflection can enumerate a specialization's arguments but cannot tell which are
    value-bearing, so this count is the one bit of per-template knowledge welder
    records; add a row to teach it a further wrapper. A template *absent* from the
    table is an opaque bindable leaf (its elements are not recursed) — e.g. a
    third-party container carrying its own converter.
*/
struct wrapper_spec {
    std::meta::info tmpl;  /**< The class template, e.g. `^^std::vector`. */
    std::size_t values;    /**< Leading arguments to recurse; `0` = all. */
};

/** The element-wise STL-wrapper table welder recurses into. @see wrapper_spec */
consteval std::array<wrapper_spec, 18> stl_wrappers() {
    return {{
        {^^std::vector, 1},        {^^std::list, 1},
        {^^std::deque, 1},         {^^std::set, 1},
        {^^std::multiset, 1},      {^^std::unordered_set, 1},
        {^^std::unordered_multiset, 1},
        {^^std::map, 2},           {^^std::multimap, 2},
        {^^std::unordered_map, 2}, {^^std::unordered_multimap, 2},
        {^^std::optional, 1},      {^^std::shared_ptr, 1},
        {^^std::unique_ptr, 1},    {^^std::pair, 2},
        {^^std::array, 1},         {^^std::tuple, 0},
        {^^std::variant, 0},
    }};
}

/** How many leading arguments of @a type to recurse if it is a listed wrapper.

    @param type a reflection of the (possibly wrapped) type.
    @return the number of leading value arguments (a `0` count in the table expands
            to all of them); `-1` if @a type is not a listed element-wise wrapper.
*/
consteval long wrapper_value_count(std::meta::info type) {
    if (!std::meta::has_template_arguments(type))
        return -1;
    const std::meta::info tmpl{std::meta::template_of(type)};
    for (const wrapper_spec& w : stl_wrappers())
        if (w.tmpl == tmpl)
            return w.values != 0 ? static_cast<long>(w.values)
                                 : static_cast<long>(
                                       std::meta::template_arguments_of(type).size());
    return -1;
}

/** The first @a N template arguments of @a Type, as a splice-ready static array.
    @tparam Type the specialization to read.
    @tparam N    how many leading arguments to take.
*/
template <std::meta::info Type, std::size_t N>
consteval std::array<std::meta::info, N> leading_args() {
    std::array<std::meta::info, N> out{};
    std::size_t i{0};
    for (auto arg : std::meta::template_arguments_of(Type)) {
        if (i == N)
            break;
        out[i++] = arg;
    }
    return out;
}

/** Whether every one of a wrapper's value arguments (spliced back to types) binds.
    @tparam B    the backend.
    @tparam L    the target language.
    @tparam Args the static array of value-argument reflections.
    @tparam I    the index pack over @a Args.
*/
template <caster_oracle B, lang L, auto Args, std::size_t... I>
consteval bool args_bindable(std::index_sequence<I...>) {
    return (bindable<B, typename [:Args[I]:], L>() && ...);
}

/** Can the backend convert @a T — stripped of cv/ref/pointer — to a meaningful
    target value?

    A listed wrapper is bindable iff its value arguments are; a type with a
    native/user converter binds as-is; otherwise it is a program-defined class/enum
    the backend must register, so it binds iff it is welded for @a L.
    @tparam B the backend.
    @tparam T the type to test.
    @tparam L the target language.
*/
template <caster_oracle B, class T, lang L>
consteval bool bindable() {
    // Strip cv/ref/pointer so the STL-wrapper table below sees the bare
    // specialization (a parameter may arrive as `const std::vector<Foo>&`).
    // dealias because remove_* are alias templates: their reflection is an alias
    // carrying neither template arguments nor annotations — the underlying type
    // does. remove_cvref handles the ref+cv combo, then remove_pointer, then a
    // final remove_cv for a pointee's constness (`const Foo*` -> `Foo`).
    constexpr std::meta::info u{std::meta::dealias(
        ^^std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<T>>>)};
    if constexpr (welder::trust_bindable<typename [:u:]>) {
        // Type-level escape hatch: the user vouches this type is registered /
        // convertible outside welder's view. Checked first, and on every recursion
        // level, so trusting `Foo` also clears `Foo*`, `const Foo&` and
        // `std::vector<Foo>` (whose element recurses back to a trusted `Foo`).
        return true;
    } else {
        constexpr long values{wrapper_value_count(u)};
        if constexpr (values >= 0) {
            constexpr auto args{leading_args<u, static_cast<std::size_t>(values)>()};
            return args_bindable<B, L, args>(std::make_index_sequence<args.size()>{});
        } else if constexpr (B::template has_native_caster<typename [:u:]>) {
            return true;
        } else {
            return welder::welded_for(u, L);
        }
    }
}

} // namespace detail

/** Is @a T bindable to language @a L under backend @a B? (public spelling.)
    @tparam B the backend.
    @tparam T the type to test.
    @tparam L the target language.
*/
template <caster_oracle B, class T, lang L>
consteval bool bindable() {
    return detail::bindable<B, T, L>();
}

/** Hard error the instant welder would bind an unbindable type.

    The offending type is the template argument of the failing instantiation, so it
    is named in the diagnostic backtrace.
    @tparam B the backend.
    @tparam T the type being bound.
    @tparam L the target language.
*/
template <caster_oracle B, class T, lang L>
consteval void assert_bindable() {
    static_assert(
        bindable<B, T, L>(),
        "welder: cannot bind this C++ type to the target language. Weld it with "
        "[[=welder::weld(...)]], or register a backend type converter for it; "
        "otherwise mark::exclude the member that uses it. The offending type is "
        "the template argument of this assert_bindable<B, T, L> instantiation.");
}

namespace detail {

/** Assert every parameter type of @a Fn binds.
    @tparam B  the backend.
    @tparam Fn a reflection of the function.
    @tparam L  the target language.
    @tparam I  the index pack over the parameters.
*/
template <caster_oracle B, std::meta::info Fn, lang L, std::size_t... I>
consteval void assert_params_bindable(std::index_sequence<I...>) {
    static constexpr auto params{param_types<Fn>()};
    (assert_bindable<B, typename [:params[I]:], L>(), ...);
}

} // namespace detail

/** Assert every parameter type and the (non-void) return type of @a Fn binds.

    So the function/method/operator/constructor can round-trip through the target
    language. A constructor has no return type.
    @tparam B  the backend.
    @tparam Fn a reflection of the callable.
    @tparam L  the target language.
*/
template <caster_oracle B, std::meta::info Fn, lang L>
consteval void assert_signature_bindable() {
    // Guard n != 0: param_types<Fn> materializes a std::array<info, n>, and
    // std::array<info, 0>::operator[] is not consteval (it must not be
    // instantiated for a parameterless function).
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    if constexpr (n != 0)
        detail::assert_params_bindable<B, Fn, L>(std::make_index_sequence<n>{});
    if constexpr (!std::meta::is_constructor(Fn)) {
        using R = [:std::meta::return_type_of(Fn):];
        if constexpr (!std::is_void_v<R>)
            assert_bindable<B, R, L>();
    }
}

// --- member-aware asserts (honor the trust_bindable member mark) -------------
//
// The driver uses these at each emission site: they run the bindability gate on a
// member's type / a callable's signature *unless* the member carries a
// [[=welder::mark::trust_bindable]] mark for L, in which case the user has vouched
// for the type and the gate is skipped. (The type-level trust_bindable<T> point is
// folded into bindable() itself, above, so it needs no per-site handling.)

/** Assert the type of a data member / namespace variable binds, unless trusted.
    @tparam B      the backend.
    @tparam Member a reflection of the data member or variable.
    @tparam L      the target language.
*/
template <caster_oracle B, std::meta::info Member, lang L>
consteval void assert_member_bindable() {
    if constexpr (!welder::trusted_for(Member, L))
        assert_bindable<B, typename [:std::meta::type_of(Member):], L>();
}

/** Assert a function/method/operator/constructor signature binds, unless trusted.
    @tparam B  the backend.
    @tparam Fn a reflection of the callable.
    @tparam L  the target language.
*/
template <caster_oracle B, std::meta::info Fn, lang L>
consteval void assert_callable_bindable() {
    if constexpr (!welder::trusted_for(Fn, L))
        assert_signature_bindable<B, Fn, L>();
}

} // namespace welder
