#pragma once
/** @file
    welder sol2 Lua backend (header-only).

    This is a *thin* backend: it implements welder's backend contract
    (`<welder/backend.hpp>`) for [sol2](https://github.com/ThePhD/sol2) and hands
    the traversal/resolution off to welder's generic driver. All the
    language-agnostic work — deciding which members bind, gating bindability,
    walking bases and namespaces — lives in the core; only the sol2 emission
    primitives are here. It targets a **loadable Lua C module**: Lua's `require`
    finds the shared object on `package.cpath` and enters it through the
    `luaopen_<name>` symbol, which welder's entry macro emits; the module registers
    its types onto a fresh table and returns it. sol2 is used through a
    `sol::state_view` over the *borrowed* `lua_State*` (welder does not own the
    interpreter).

    Requires the welder vocabulary to be available first, via either `import
    welder;` (module form) or `#include <welder/welder.hpp>` (header-only). Then:
    @code
    #include <sol/sol.hpp>
    #include <welder/backends/lua/sol2/backend.hpp>
    extern "C" int luaopen_mymod(lua_State* L) {
        sol::state_view lua(L);
        sol::table m = lua.create_table();
        welder::sol2::bind<MyType>(m);
        return sol::stack::push(L, m);
    }
    @endcode
    or, more simply, via the backend-agnostic entry macro:
    @code
    WELDER_MODULE(mymod, sol2) {}   // binds namespace ^^mymod into luaopen_mymod
    @endcode

    ## How Lua differs from the Python backends

    - **Metamethods, not dunders.** Member operators bind to Lua *metamethods*
      (`__add`, `__eq`, …), and Lua's set is smaller and asymmetric: it has no
      `__ne`/`__gt`/`__ge` — the VM derives `~=`, `>` and `>=` from `__eq`, `__lt`
      and `__le` with swapped operands — so those C++ operators map to *nothing*
      (they still work in Lua, synthesized). Bitwise metamethods exist only on Lua
      ≥ 5.3 (not on LuaJIT's 5.1 ABI); they are `#if`-gated. See @ref operator_mm.
    - **Constructors, all at once.** sol2 takes a type's whole constructor set in one
      `sol::constructors<…>`, unlike pybind11's incremental `.def(init<>())`. So this
      backend registers constructors from reflection inside @ref make_class, and the
      driver's per-constructor hooks (`add_default_ctor`, `add_constructor`,
      `add_aggregate_constructor`) are intentional no-ops here.
    - **Enums are tables.** Lua has no enum type; a welded enum becomes a plain
      name→integer table (an unscoped enum's names are also mirrored onto the
      enclosing module, like C++).
    - **No runtime docstrings.** Lua has no `__doc__` slot, so `[[=welder::doc]]` /
      `returns` are not surfaced at runtime; their home is a generated LuaCATS
      (`---@meta`) stub, a separate emitter (not part of this binding backend).
    - **Namespace variables snapshot.** A namespace variable binds as a value
      snapshot at load time (const and mutable alike); live get/set over a C++ global
      is a planned enhancement (a metatable proxy over the module table).

    @note Overloaded methods, static methods, free functions and operators: sol2
    stores one value per name (and per metamethod slot), so welder gathers a name's
    several C++ overloads into one `sol::overload(…)` — sol2 dispatches among them at
    call time. The gathering happens in the backend (see @ref method_overloads),
    because the driver visits each overload individually to suit pybind11's
    incremental `.def`. A same-named member in a derived class still hides the base's
    (C++ name-hiding), as it did before.
*/
#include <array>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

#include <welder/backend.hpp>     // the backend contract + generic driver
#include <welder/bind_traits.hpp> // is_bindable_constructor / aggregate_* helpers
#include <welder/module.hpp>      // WELDER_MODULE dispatch (entry-point macro)

#include <sol/sol.hpp>

namespace welder::sol2 {

// Note: the namespace is `welder::sol2`; the library namespace is `::sol`. They do
// not collide, so `sol::` below refers to the library without an alias (unlike the
// Python backends, whose namespace shadowed the library's).

namespace detail {

/** Whether sol2 can only convert @a T via *runtime usertype registration*.

    sol2 classifies every type at compile time via `sol::lua_type_of<T>`; a value
    that maps to `sol::type::userdata` is a program-defined class sol2 can convert
    only once a `sol::usertype` has been registered for it. Scalars, `bool`,
    strings and the sol2 wrapper types (`sol::object`, `sol::table`, …) map to
    `number`/`string`/`table`/`poly`/… and convert natively. This is the one
    bindability fact welder's core cannot know; it drives `caster_oracle` below.

    Enums are forced into the needs-registration bucket even though sol2 would
    convert them as plain numbers: welder wants a welded enum registered (as its
    name→value table, for named access) and its enum-typed members gated on that,
    matching the Python backends. The STL-wrapper recursion in `bindable.hpp` is
    shared, so containers/optional/variant/smart-pointers of a native/welded type
    are handled without reaching here.

    Like the Python backends' counterparts this is *conservative*: it reads @a T's
    static classification, so it reports whether @a T *needs* registration, never
    whether one exists at runtime. A type hand-registered out-of-band still reads
    `true`; that false positive is resolved by the deferred `trust_bindable`
    escape hatch.

    @tparam T the type whose sol2 classification to read.
*/
template <class T>
inline constexpr bool needs_registration =
    std::is_enum_v<std::remove_cvref_t<T>> ||
    ::sol::lua_type_of<std::remove_cvref_t<T>>::value == ::sol::type::userdata;

/** A member operator's Lua metamethod, or `{…, nullptr}` if welder does not expose
    it.

    Unary vs binary is told apart by arity (a member operator takes 0 parameters
    when unary, 1 when binary), disambiguating the operators with both forms
    (`+`, `-`). The map reflects Lua's metamethod model, which differs from Python's
    dunders in three ways:
    - **No `__ne`/`__gt`/`__ge`.** Lua synthesizes `~=`, `>` and `>=` from `__eq`,
      `__lt` and `__le` (operands swapped), so `operator!=`, `operator>` and
      `operator>=` map to nothing — they already work in Lua once `==`/`<`/`<=` are
      bound.
    - **`^` is XOR, not power.** C++ `operator^` maps to Lua's bitwise-xor
      metamethod `__bxor`, not `__pow` (which is Lua's `^`).
    - **Bitwise metamethods are Lua ≥ 5.3 only** (`__band`, `__bor`, `__bxor`,
      `__bnot`, `__shl`, `__shr`) — absent on Lua 5.1 / LuaJIT — so they are
      `#if`-gated on `LUA_VERSION_NUM`.

    In-place compound assignments (`operator+=`, …), `<=>`, `&&`, `||`, `++`, `--`
    and `=` are not mapped (same as the Python backends).

    @param f a reflection of the operator function.
    @return the `sol::meta_function` and its `__name`, or a `nullptr` name.
*/
struct metamethod {
    ::sol::meta_function fn; /**< The sol2 metamethod slot. */
    const char* name;        /**< Its `__name`, or `nullptr` if not exposed. */
};
consteval metamethod operator_mm(std::meta::info f) {
    using std::meta::operators;
    using mf = ::sol::meta_function;
    const bool unary{welder::detail::is_unary_operator(f)};
    const metamethod none{mf::index, nullptr};
    switch (std::meta::operator_of(f)) {
        case operators::op_plus:
            return unary ? none : metamethod{mf::addition, "__add"}; // no unary +
        case operators::op_minus:
            return unary ? metamethod{mf::unary_minus, "__unm"}
                         : metamethod{mf::subtraction, "__sub"};
        case operators::op_star:
            return unary ? none // unary * is dereference, not a Lua metamethod
                         : metamethod{mf::multiplication, "__mul"};
        case operators::op_slash:   return {mf::division, "__div"};
        case operators::op_percent: return {mf::modulus, "__mod"};
        case operators::op_equals_equals: return {mf::equal_to, "__eq"};
        case operators::op_less:          return {mf::less_than, "__lt"};
        case operators::op_less_equals:   return {mf::less_than_or_equal_to, "__le"};
        // Lua derives ~=, > and >= from __eq/__lt/__le, so these expose nothing.
        case operators::op_exclamation_equals:
        case operators::op_greater:
        case operators::op_greater_equals: return none;
        case operators::op_parentheses:    return {mf::call, "__call"};
        // operator[] -> Lua's __index (a sol2 fallback consulted after normal
        // member/method lookup, so it coexists with fields and methods).
        case operators::op_square_brackets: return {mf::index, "__index"};
#if defined(LUA_VERSION_NUM) && LUA_VERSION_NUM >= 503
        // Bitwise: Lua 5.3+ only. C++ operator^ is XOR (Lua __bxor), NOT power.
        case operators::op_caret:     return {mf::bitwise_xor, "__bxor"};
        case operators::op_tilde:
            return unary ? metamethod{mf::bitwise_not, "__bnot"} : none;
        case operators::op_ampersand:
            return unary ? none // unary & is address-of
                         : metamethod{mf::bitwise_and, "__band"};
        case operators::op_pipe:            return {mf::bitwise_or, "__bor"};
        case operators::op_less_less:       return {mf::bitwise_left_shift, "__shl"};
        case operators::op_greater_greater: return {mf::bitwise_right_shift, "__shr"};
#endif
        default: return none;
    }
}

/** The alias `T(A...)` — a constructor call signature, built by `substitute`. */
template <class T, class... A>
using ctor_sig = T(A...);

/** The set of `sol::constructors<…>` signatures to expose for @a T, as function-
    type reflections `T(A...)`.

    Mirrors the driver's constructor selection (default constructor, each public
    non-copy/move constructor, and — for a baseless aggregate whose fields all bind
    — a field constructor via C++26 parenthesized aggregate init), but gathered in
    one place because sol2 wants the whole set at once. Each signature is a
    `substitute`d, dealiased `ctor_sig<T, Params…>`.

    @tparam T the class type.
    @return the constructor-signature reflections (may be empty: a type with no
            Lua-constructible form).
*/
template <class T>
consteval std::vector<std::meta::info> ctor_signatures() {
    std::vector<std::meta::info> sigs;
    constexpr auto ctx{std::meta::access_context::unchecked()};
    auto sig = [](std::vector<std::meta::info> targs) {
        return std::meta::dealias(std::meta::substitute(^^ctor_sig, targs));
    };
    if (std::is_default_constructible_v<T>)
        sigs.push_back(sig({^^T}));
    for (auto c : std::meta::members_of(^^T, ctx))
        if (welder::detail::is_bindable_constructor(c)) {
            std::vector<std::meta::info> targs{^^T};
            for (auto p : std::meta::parameters_of(c))
                targs.push_back(std::meta::type_of(p));
            sigs.push_back(sig(targs));
        }
    if (welder::detail::aggregate_initializable<T, lang::lua>()) {
        std::vector<std::meta::info> targs{^^T};
        for (auto fld : welder::detail::aggregate_fields<T>())
            targs.push_back(std::meta::type_of(fld));
        sigs.push_back(sig(targs));
    }
    return sigs;
}

/** `ctor_signatures<T>()` as a fixed-size, splice-ready static array. */
template <class T>
consteval auto ctor_sigs_array() {
    constexpr std::size_t n{ctor_signatures<T>().size()};
    std::array<std::meta::info, n> out{};
    // Guard the fill: std::array<T, 0>::operator[] is not consteval, so it must
    // not be instantiated for a type with no exposed constructors.
    if constexpr (n != 0) {
        auto v{ctor_signatures<T>()};
        for (std::size_t i{0}; i < n; ++i)
            out[i] = v[i];
    }
    return out;
}

/** Register @a T's constructor set on the usertype from the reflected signatures. */
template <class T, auto Sigs, std::size_t... I>
void set_constructors(::sol::usertype<T>& ut, std::index_sequence<I...>) {
    // Expose both the call form `T(…)` and the idiomatic `T.new(…)`.
    ut[::sol::call_constructor] = ::sol::constructors<typename [:Sigs[I]:]...>();
    ut["new"] = ::sol::constructors<typename [:Sigs[I]:]...>();
}

/** @see set_constructors — no-op when @a T exposes no Lua constructor. */
template <class T>
void register_constructors(::sol::usertype<T>& ut) {
    constexpr auto sigs{ctor_sigs_array<T>()};
    if constexpr (sigs.size() != 0)
        set_constructors<T, sigs>(ut, std::make_index_sequence<sigs.size()>{});
}

/** Create `m.new_usertype<T, Bases…>(name)` with sol2's base-class linkage.

    `sol::no_constructor` suppresses sol2's implicit constructor; the real set is
    installed by register_constructors(). The base list is passed only when
    non-empty (sol2 accepts several bases, so multiple inheritance binds here).
    @tparam T     the class type.
    @tparam Bases the static array of native (welded) base type reflections.
    @tparam I     the base index pack.
    @param m    the module table.
    @param name the Lua type name.
*/
template <class T, auto Bases, std::size_t... I>
auto make_usertype(::sol::table& m, const char* name, std::index_sequence<I...>) {
    if constexpr (sizeof...(I) == 0) {
        ::sol::usertype<T> ut{m.new_usertype<T>(name, ::sol::no_constructor)};
        register_constructors<T>(ut);
        return ut;
    } else {
        ::sol::usertype<T> ut{m.new_usertype<T>(
            name, ::sol::no_constructor, ::sol::base_classes,
            ::sol::bases<typename [:Bases[I]:]...>())};
        register_constructors<T>(ut);
        return ut;
    }
}

/** Collect *all* welded ancestors of @a type (transitively), deduplicated.

    sol2 differs from pybind11 here: `sol::bases<…>` must list every base class a
    usertype should upcast to / inherit members from, including indirect ones — it
    does not chain through an intermediate usertype's own bases. So where welder's
    core (`native_base_types`) collects only the *nearest* welded ancestors (enough
    for pybind11, which links each level to its own base), this walks past welded
    bases too, gathering the full closure. Non-welded bases are still descended
    through (their members are flattened by the driver); a virtual diamond reaches a
    shared base by several paths, so the list is deduplicated.

    @param type a reflection of the derived type.
    @param L    the target language.
    @param[out] out accumulates the deduplicated welded-ancestor reflections.
*/
consteval void collect_welded_bases(std::meta::info type, lang L,
                                    std::vector<std::meta::info>& out) {
    for (auto base : welder::public_bases(type)) {
        const bool welded{welder::welded_for(base, L)};
        if (welded) {
            bool seen{false};
            for (auto e : out)
                if (e == base) {
                    seen = true;
                    break;
                }
            if (seen)
                continue;
            out.push_back(base);
        }
        collect_welded_bases(base, L, out); // descend (into welded bases too)
    }
}

/** `collect_welded_bases` for @a T as a fixed-size, splice-ready static array. */
template <class T>
consteval auto welded_bases_array() {
    constexpr std::size_t n{[] {
        std::vector<std::meta::info> v;
        collect_welded_bases(^^T, lang::lua, v);
        return v.size();
    }()};
    std::array<std::meta::info, n> out{};
    // Guard the fill: std::array<T, 0>::operator[] is not consteval.
    if constexpr (n != 0) {
        std::vector<std::meta::info> v;
        collect_welded_bases(^^T, lang::lua, v);
        for (std::size_t i{0}; i < n; ++i)
            out[i] = v[i];
    }
    return out;
}

/** A welded enum's binding: the name→value table, the enclosing module, and
    whether the enum is scoped (an unscoped enum also mirrors its names onto the
    module, like C++). */
template <class E>
struct enum_binding {
    ::sol::table values;      /**< The `E = { Name = value, … }` table. */
    ::sol::table parent;      /**< The enclosing module table. */
    bool scoped;              /**< `true` for `enum class`. */
};

// --- overload grouping ------------------------------------------------------
//
// sol2 stores a single value per name (and per metamethod slot), so several C++
// overloads sharing a name must be gathered into one `sol::overload(…)` — sol2
// wants the whole set at once, exactly as it does for a type's constructors (see
// ctor_signatures). The driver still visits each overload individually (that suits
// pybind11's incremental `.def`), so the backend gathers the siblings itself; the
// selection predicates live in the core (`bind_traits.hpp`, shared with the LuaCATS
// stub backend, which also gathers overloads), re-invoked here so a group is exactly
// what the driver binds.
using welder::detail::function_overload_set;
using welder::detail::is_overload_leader;
using welder::detail::method_overload_set;
using welder::detail::operator_overload_set;
using welder::detail::overload_group;

/** Register overload group @a Grp on target @a t under @a Grp[0]'s identifier — a
    single callable when unique, a `sol::overload(…)` when several. Each overload is
    spliced by its specific reflection, so `&[:Grp[i]:]` is the exact overload (no
    `&C::f` ambiguity). Serves methods, static methods and free functions alike
    (sol2 registers all three as a table entry). */
template <auto Grp, class Target, std::size_t... I>
void register_named(Target& t, std::index_sequence<I...>) {
    constexpr const char* name{
        std::define_static_string(std::meta::identifier_of(Grp[0]))};
    if constexpr (sizeof...(I) == 1)
        t[name] = &[:Grp[0]:];
    else
        t[name] = ::sol::overload(&[:Grp[I]:]...);
}

/** Register operator group @a Grp under its Lua metamethod slot (a single callable,
    or a `sol::overload(…)` for several same-slot overloads). */
template <auto Grp, class Target, std::size_t... I>
void register_operator(Target& t, std::index_sequence<I...>) {
    constexpr auto slot{operator_mm(Grp[0]).fn};
    if constexpr (sizeof...(I) == 1)
        t[slot] = &[:Grp[0]:];
    else
        t[slot] = ::sol::overload(&[:Grp[I]:]...);
}

/** The sol2 backend: a stateless policy type satisfying @ref welder::backend.

    Its static members are the sol2 emission primitives welder's driver calls; the
    driver supplies all the reflection-derived decisions. See `<welder/backend.hpp>`
    for the contract each member fulfills.
*/
struct backend {
    static constexpr lang language{lang::lua}; /**< welder::lang::lua. */
    using module_type = ::sol::table;          /**< A Lua module is a table. */

    /** An unused per-module session (Lua needs no deferred module state yet;
        namespace variables bind eagerly as snapshots). */
    struct session {};

    /** `caster_oracle`: @a T converts without welder registering a usertype iff
        sol2 does not classify it as needs-registration. @see needs_registration */
    template <class T>
    static constexpr bool has_native_caster = !needs_registration<T>;

    /** Map a member operator to its Lua metamethod name (`nullptr` = not exposed). */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return operator_mm(op_fn).name;
    }

    // --- class binding ------------------------------------------------------

    /** Create the `sol::usertype<T>` handle (constructors installed from
        reflection; @a doc has no Lua runtime home and is ignored).

        The driver's @a Bases is welder's *nearest* welded-ancestor set; sol2 needs
        the full transitive closure (see collect_welded_bases), so this recomputes
        it from @a T and ignores the passed-in @a Bases. @see make_usertype */
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* /*doc*/,
                           std::index_sequence<I...> /*seq*/) {
        constexpr auto bases{welded_bases_array<T>()};
        return make_usertype<T, bases>(
            m, name, std::make_index_sequence<bases.size()>{});
    }

    // Constructors are registered as one set in make_class (sol2 wants them all at
    // once), so the driver's per-constructor hooks are intentional no-ops here.
    static void add_default_ctor(auto&) {}
    template <std::meta::info /*Ctor*/>
    static void add_constructor(auto&) {}
    template <class /*T*/>
    static void add_aggregate_constructor(auto&) {}

    /** Bind data member @a Mem as a usertype property.

        A mutable member becomes read/write; a const member is wrapped in
        `sol::readonly` (its setter would not compile). Lua has no property
        docstring, so a `[[=welder::doc]]` on the member is not surfaced at runtime
        (it belongs in a generated stub). */
    template <std::meta::info Mem>
    static void add_field(auto& ut) {
        constexpr const char* name{
            std::define_static_string(std::meta::identifier_of(Mem))};
        if constexpr (std::meta::is_const_type(std::meta::type_of(Mem)))
            ut[name] = ::sol::readonly(&[:Mem:]);
        else
            ut[name] = &[:Mem:];
    }

    /** Bind member function @a Fn as a method (`obj:name(…)`). Several C++ overloads
        of a name are gathered into one `sol::overload(…)`; the driver visits each
        overload, so the whole group is registered once, on its first member. */
    template <std::meta::info Fn>
    static void add_method(auto& ut) {
        if constexpr (is_overload_leader<method_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<method_overload_set, Fn, lang::lua>()};
            register_named<grp>(ut, std::make_index_sequence<grp.size()>{});
        }
    }

    /** Bind static member function @a Fn as a class-table function (`T.name(…)`);
        overloads are grouped as in @ref add_method. */
    template <std::meta::info Fn>
    static void add_static_method(auto& ut) {
        if constexpr (is_overload_leader<method_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<method_overload_set, Fn, lang::lua>()};
            register_named<grp>(ut, std::make_index_sequence<grp.size()>{});
        }
    }

    /** Bind member operator @a Fn under its Lua metamethod. The specific overload is
        spliced, so unary/binary forms never collide; several overloads mapping to
        the *same* metamethod slot are gathered into one `sol::overload(…)`. */
    template <std::meta::info Fn>
    static void add_operator(auto& ut) {
        if constexpr (is_overload_leader<operator_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<operator_overload_set, Fn, lang::lua>()};
            register_operator<grp>(ut, std::make_index_sequence<grp.size()>{});
        }
    }

    // --- enum binding -------------------------------------------------------

    /** Create the enum's `Name = value` table on the module (@a doc ignored). */
    template <class E>
    static auto make_enum(module_type& m, const char* name, const char* /*doc*/) {
        ::sol::table values{::sol::state_view{m.lua_state()}.create_table()};
        m[name] = values;
        return enum_binding<E>{values, m, std::is_scoped_enum_v<E>};
    }

    /** Add enumerator @a Enum (as its underlying integer). An unscoped enum's
        enumerator is also mirrored onto the enclosing module, unqualified — the
        driver's `finish_enum` role, done incrementally here since the handle
        carries the parent. */
    template <std::meta::info Enum>
    static void add_enumerator(auto& e) {
        constexpr const char* name{
            std::define_static_string(std::meta::identifier_of(Enum))};
        const lua_Integer value{static_cast<lua_Integer>(std::to_underlying([:Enum:]))};
        e.values[name] = value;
        if (!e.scoped)
            e.parent[name] = value;
    }

    /** No whole-enum finalizer needed (unscoped export is done per-enumerator). */
    template <class /*E*/>
    static void finish_enum(auto&) {}

    // --- namespace / module binding -----------------------------------------

    /** Open the (empty) per-module session. */
    static session open_module(module_type&) { return {}; }

    /** No runtime module docstring in Lua (its home is a generated stub). */
    static void set_module_doc(module_type&, const char*) {}

    /** Bind free function @a Fn as a module-level function; overloads are gathered
        into one `sol::overload(…)`, registered once on the group's first member. */
    template <std::meta::info Fn>
    static void add_function(module_type& m) {
        if constexpr (is_overload_leader<function_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<function_overload_set, Fn, lang::lua>()};
            register_named<grp>(m, std::make_index_sequence<grp.size()>{});
        }
    }

    /** Bind namespace variable @a Var as a value snapshot at load time.

        Both const and mutable variables snapshot for now; a live get/set property
        over the C++ global (via a metatable proxy on the module table) is a planned
        enhancement. */
    template <std::meta::info Var>
    static void add_variable(module_type& m, session&) {
        m[std::define_static_string(std::meta::identifier_of(Var))] = [:Var:];
    }

    /** Create a submodule table named @a name under @a m. */
    static module_type add_submodule(module_type& m, const char* name) {
        ::sol::table sub{::sol::state_view{m.lua_state()}.create_table()};
        m[name] = sub;
        return sub;
    }

    /** Close the session (nothing deferred). */
    static void close_module(module_type&, session&) {}
};

} // namespace detail

/** Reflect over @a T and register it on module table @a m.

    A class type becomes a `sol::usertype`; an enum becomes a name→value table (its
    enumerators resolve like data members, honoring the enum's policy/marks). See
    the driver in `<welder/backend.hpp>` for the full set of what is bound.

    @tparam T the type to bind.
    @param m    the module table.
    @param name the Lua name, or `nullptr` to default to @a T's identifier.
    @return the usertype/enum-binding handle, so callers can chain further work.
*/
template <class T>
auto bind(::sol::table& m, const char* name = nullptr) {
    if constexpr (std::is_enum_v<T>)
        return welder::detail::bind_enum<detail::backend, T>(m, name);
    else
        return welder::detail::bind_type<detail::backend, T>(m, name);
}

/** Reflect over a whole namespace and expose its welded members on module @a m.

    Classes bind via bind(), free functions and namespace variables become module
    fields, and nested namespaces become submodule tables. Usage:
    @code
    extern "C" int luaopen_mymod(lua_State* L) {
        sol::state_view lua(L);
        sol::table m = lua.create_table();
        welder::sol2::bind_namespace<^^myns>(m);
        return sol::stack::push(L, m);
    }
    @endcode

    @tparam Ns a reflection of the namespace.
    @param m the module table.
*/
template <std::meta::info Ns>
void bind_namespace(::sol::table& m) {
    welder::detail::bind_namespace_driver<detail::backend, Ns>(m);
}

/** A do-nothing module hook; the default for build_module()'s pre/post callbacks. */
inline constexpr auto noop = [](::sol::table&) {};

/** Build a whole Lua module out of top-level C++ namespace @a Ns.

    Runs @a pre, binds the namespace into @a m, then runs @a post. The hooks fold
    hand-written bindings in around welder's generated body. This fills an existing
    module table; pair it with an entry point that emits `luaopen_<name>` and
    returns the table (welder's `WELDER_MODULE(ns, sol2)` does both):
    @code
    WELDER_MODULE(shapes, sol2) {}
    @endcode

    @tparam Ns   a reflection of the top-level namespace.
    @tparam Pre  the pre-hook callable type.
    @tparam Post the post-hook callable type.
    @param m    the module table to fill.
    @param pre  invoked with @a m before binding (defaults to noop).
    @param post invoked with @a m after binding (defaults to noop).
*/
template <std::meta::info Ns, class Pre = decltype(noop), class Post = decltype(noop)>
void build_module(::sol::table& m, Pre pre = noop, Post post = noop) {
    welder::detail::build_module_driver<detail::backend, Ns>(m, pre, post);
}

} // namespace welder::sol2

/** @def WELDER_DETAIL_MODULE_ENTRY_sol2
    sol2's expansion of the backend-agnostic `WELDER_MODULE(ns, sol2)`.

    Emits the `luaopen_<ns>` C entry point Lua's `require` calls: it views the
    borrowed `lua_State*` with a `sol::state_view`, creates the module table, binds
    namespace `^^ns` into it, runs the optional trailing `{ }` block as post-glue
    (with the module table named `module` in scope), and returns the table to Lua.
    The block is supplied as the body of a forward-declared, internally-linked glue
    function (the technique the Python backends' entry macros use), so both
    `WELDER_MODULE(ns, sol2) { … }` and `WELDER_MODULE(ns, sol2) {}` work. Defined at
    file scope (macros ignore namespaces); see `<welder/module.hpp>`.
    @param ns the namespace / module name token (doubles as the `luaopen_` symbol).
*/
#define WELDER_DETAIL_MODULE_ENTRY_sol2(ns)                                       \
    static void welder_glue_##ns##_sol2(::sol::table&);                           \
    extern "C" int luaopen_##ns(lua_State* welder_lua_state_) {                   \
        ::sol::state_view welder_lua_{welder_lua_state_};                         \
        ::sol::table welder_module_var_{welder_lua_.create_table()};             \
        ::welder::sol2::build_module<^^ns>(                                       \
            welder_module_var_, ::welder::sol2::noop,                             \
            [](::sol::table& welder_glue_m_) {                                    \
                welder_glue_##ns##_sol2(welder_glue_m_);                          \
            });                                                                   \
        return ::sol::stack::push(welder_lua_state_, welder_module_var_);         \
    }                                                                             \
    static void welder_glue_##ns##_sol2(::sol::table& module)
