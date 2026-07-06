#pragma once
/** @file
    welder sol2 Lua rod (header-only).

    This is a *thin* rod: it implements welder's rod contract
    (`<welder/welder.hpp>`) for [sol2](https://github.com/ThePhD/sol2) and hands
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
    welder;` (module form) or `#include <welder/vocabulary.hpp>` (header-only).
    This header exposes exactly one thing: the rod type
    `welder::rods::sol2::rod`, to plug into `welder::welder`:
    @code
    #include <sol/sol.hpp>
    #include <welder/rods/lua/sol2/rod.hpp>
    extern "C" int luaopen_mymod(lua_State* L) {
        sol::state_view lua(L);
        sol::table m = lua.create_table();
        welder::welder<welder::rods::sol2::rod>::weld_type<MyType>(m);
        return sol::stack::push(L, m);
    }
    @endcode
    or, more simply, via the backend-agnostic entry macro (include this
    directory's `module.hpp` instead):
    @code
    WELDER_MODULE(mymod, sol2) {}   // welds namespace ^^mymod into luaopen_mymod
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

#include <welder/welder.hpp>           // welder::welder + rod contract + driver
#include <welder/rods/lua/overloads.hpp> // shared Lua overload-set selectors
#include <welder/rods/lua/sol2/metamethods.hpp> // operator -> sol2 metamethod map
#include <welder/bind_traits.hpp>      // is_bindable_constructor / aggregate_* helpers

#include <sol/sol.hpp>

namespace welder::rods::sol2 {

// Note: the namespace is `welder::rods::sol2`; the library namespace is `::sol`. They do
// not collide, so `sol::` below refers to the library without an alias (unlike the
// Python backends, whose namespace shadowed the library's).

/** The alias `T(A...)` — a constructor call signature, built by `substitute`
    (a namespace-scope alias so it can be reflected as `^^ctor_sig`). */
template <class T, class... A>
using ctor_sig = T(A...);

// The overload-set selectors are shared with the LuaCATS stub backend
// (`<welder/rods/lua/overloads.hpp>`; both gather a name's C++ overloads that the
// driver visits one at a time) and reuse the core eligibility predicates, so a
// group is exactly what the driver binds.
using ::welder::rods::lua::function_overload_set;
using ::welder::rods::lua::is_overload_leader;
using ::welder::rods::lua::method_overload_set;
using ::welder::rods::lua::operator_overload_set;
using ::welder::rods::lua::overload_group;

/** The sol2 rod: a stateless policy type satisfying @ref welder::rod.

    Its public static members are the sol2 emission primitives welder's driver
    calls; the driver supplies all the reflection-derived decisions. See
    `<welder/welder.hpp>` for the contract each member fulfills. The `protected`
    members below are sol2-specific implementation helpers (prefixed `_`), not part
    of the contract; the operator→metamethod map lives in `metamethods.hpp`.
*/
struct rod {
    static constexpr lang language{lang::lua}; /**< welder::lang::lua. */
    using module_type = ::sol::table;          /**< A Lua module is a table. */

    /** An unused per-module session (Lua needs no deferred module state yet;
        namespace variables bind eagerly as snapshots). */
    struct session {};

  protected:
    // --- implementation helpers (not part of the welder::rod contract) --

    /** Whether sol2 can only convert @a T via *runtime usertype registration*.

        sol2 classifies every type at compile time via `sol::lua_type_of<T>`; a
        value that maps to `sol::type::userdata` is a program-defined class sol2 can
        convert only once a `sol::usertype` has been registered for it. Scalars,
        `bool`, strings and the sol2 wrapper types (`sol::object`, `sol::table`, …)
        map to `number`/`string`/`table`/`poly`/… and convert natively. This is the
        one bindability fact welder's core cannot know; it drives `has_native_caster`
        below.

        Enums are forced into the needs-registration bucket even though sol2 would
        convert them as plain numbers: welder wants a welded enum registered (as its
        name→value table, for named access) and its enum-typed members gated on that,
        matching the Python backends. The STL-wrapper recursion in `bindable.hpp` is
        shared, so containers/optional/variant/smart-pointers of a native/welded type
        are handled without reaching here.

        Like the Python backends' counterparts this is *conservative*: it reads
        @a T's static classification, so it reports whether @a T *needs*
        registration, never whether one exists at runtime. A type hand-registered
        out-of-band still reads `true`; that false positive is resolved by the
        deferred `trust_bindable` escape hatch.

        @tparam T the type whose sol2 classification to read.
    */
    template <class T>
    static constexpr bool _needs_registration =
        std::is_enum_v<std::remove_cvref_t<T>> ||
        ::sol::lua_type_of<std::remove_cvref_t<T>>::value == ::sol::type::userdata;

    /** A welded enum's binding: the name→value table, the enclosing module, and
        whether the enum is scoped (an unscoped enum also mirrors its names onto the
        module, like C++). */
    template <class E>
    struct _enum_binding {
        ::sol::table values;      /**< The `E = { Name = value, … }` table. */
        ::sol::table parent;      /**< The enclosing module table. */
        bool scoped;              /**< `true` for `enum class`. */
    };

    /** The set of `sol::constructors<…>` signatures to expose for @a T, as function-
        type reflections `T(A...)`.

        Mirrors the driver's constructor selection (default constructor, each public
        non-copy/move constructor, and — for a baseless aggregate whose fields all
        bind — a field constructor via C++26 parenthesized aggregate init), but
        gathered in one place because sol2 wants the whole set at once. Each
        signature is a `substitute`d, dealiased `ctor_sig<T, Params…>`.

        @tparam T the class type.
        @return the constructor-signature reflections (may be empty: a type with no
                Lua-constructible form).
    */
    template <class T>
    static consteval std::vector<std::meta::info> _ctor_signatures() {
        std::vector<std::meta::info> sigs;
        constexpr auto ctx{std::meta::access_context::unchecked()};
        auto sig = [](std::vector<std::meta::info> targs) {
            return std::meta::dealias(std::meta::substitute(^^ctor_sig, targs));
        };
        if (std::is_default_constructible_v<T>)
            sigs.push_back(sig({^^T}));
        for (auto c : std::meta::members_of(^^T, ctx))
            if (::welder::detail::is_bindable_constructor(c)) {
                std::vector<std::meta::info> targs{^^T};
                for (auto p : std::meta::parameters_of(c))
                    targs.push_back(std::meta::type_of(p));
                sigs.push_back(sig(targs));
            }
        if (::welder::detail::aggregate_initializable<T, lang::lua>()) {
            std::vector<std::meta::info> targs{^^T};
            for (auto fld : ::welder::detail::aggregate_fields<T>())
                targs.push_back(std::meta::type_of(fld));
            sigs.push_back(sig(targs));
        }
        return sigs;
    }

    /** `_ctor_signatures<T>()` as a fixed-size, splice-ready static array.
        @tparam T the class type.
        @return the constructor-signature reflections as a static array. */
    template <class T>
    static consteval auto _ctor_sigs_array() {
        constexpr std::size_t n{_ctor_signatures<T>().size()};
        std::array<std::meta::info, n> out{};
        // Guard the fill: std::array<T, 0>::operator[] is not consteval, so it must
        // not be instantiated for a type with no exposed constructors.
        if constexpr (n != 0) {
            auto v{_ctor_signatures<T>()};
            for (std::size_t i{0}; i < n; ++i)
                out[i] = v[i];
        }
        return out;
    }

    /** Register @a T's constructor set on the usertype from the reflected
        signatures.
        @tparam T    the class type.
        @tparam Sigs the static array of `ctor_sig<T, …>` reflections.
        @tparam I    the signature index pack.
        @param ut the usertype to install the constructors on. */
    template <class T, auto Sigs, std::size_t... I>
    static void _set_constructors(::sol::usertype<T>& ut,
                                  std::index_sequence<I...>) {
        // Expose both the call form `T(…)` and the idiomatic `T.new(…)`.
        ut[::sol::call_constructor] = ::sol::constructors<typename [:Sigs[I]:]...>();
        ut["new"] = ::sol::constructors<typename [:Sigs[I]:]...>();
    }

    /** @see _set_constructors — no-op when @a T exposes no Lua constructor.
        @tparam T the class type.
        @param ut the usertype to install the constructors on. */
    template <class T>
    static void _register_constructors(::sol::usertype<T>& ut) {
        constexpr auto sigs{_ctor_sigs_array<T>()};
        if constexpr (sigs.size() != 0)
            _set_constructors<T, sigs>(ut, std::make_index_sequence<sigs.size()>{});
    }

    /** Create `m.new_usertype<T, Bases…>(name)` with sol2's base-class linkage.

        `sol::no_constructor` suppresses sol2's implicit constructor; the real set is
        installed by _register_constructors(). The base list is passed only when
        non-empty (sol2 accepts several bases, so multiple inheritance binds here).
        @tparam T     the class type.
        @tparam Bases the static array of native (welded) base type reflections.
        @tparam I     the base index pack.
        @param m    the module table.
        @param name the Lua type name.
    */
    template <class T, auto Bases, std::size_t... I>
    static auto _make_usertype(::sol::table& m, const char* name,
                               std::index_sequence<I...>) {
        if constexpr (sizeof...(I) == 0) {
            ::sol::usertype<T> ut{m.new_usertype<T>(name, ::sol::no_constructor)};
            _register_constructors<T>(ut);
            return ut;
        } else {
            ::sol::usertype<T> ut{m.new_usertype<T>(
                name, ::sol::no_constructor, ::sol::base_classes,
                ::sol::bases<typename [:Bases[I]:]...>())};
            _register_constructors<T>(ut);
            return ut;
        }
    }

    /** Collect *all* welded ancestors of @a type (transitively), deduplicated.

        sol2 differs from pybind11 here: `sol::bases<…>` must list every base class a
        usertype should upcast to / inherit members from, including indirect ones —
        it does not chain through an intermediate usertype's own bases. So where
        welder's core (`native_base_types`) collects only the *nearest* welded
        ancestors (enough for pybind11, which links each level to its own base), this
        walks past welded bases too, gathering the full closure. Non-welded bases are
        still descended through (their members are flattened by the driver); a virtual
        diamond reaches a shared base by several paths, so the list is deduplicated.

        @param type a reflection of the derived type.
        @param L    the target language.
        @param[out] out accumulates the deduplicated welded-ancestor reflections.
    */
    static consteval void _collect_welded_bases(
        std::meta::info type, lang L, std::vector<std::meta::info>& out) {
        for (auto base : ::welder::public_bases(type)) {
            const bool welded{::welder::welded_for(base, L)};
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
            _collect_welded_bases(base, L, out); // descend (into welded bases too)
        }
    }

    /** `_collect_welded_bases` for @a T as a fixed-size, splice-ready static array.
        @tparam T the derived class type.
        @return the deduplicated welded-ancestor reflections as a static array. */
    template <class T>
    static consteval auto _welded_bases_array() {
        constexpr std::size_t n{[] {
            std::vector<std::meta::info> v;
            _collect_welded_bases(^^T, lang::lua, v);
            return v.size();
        }()};
        std::array<std::meta::info, n> out{};
        // Guard the fill: std::array<T, 0>::operator[] is not consteval.
        if constexpr (n != 0) {
            std::vector<std::meta::info> v;
            _collect_welded_bases(^^T, lang::lua, v);
            for (std::size_t i{0}; i < n; ++i)
                out[i] = v[i];
        }
        return out;
    }

    /** Register overload group @a Grp on target @a t under @a Grp[0]'s identifier —
        a single callable when unique, a `sol::overload(…)` when several. Each
        overload is spliced by its specific reflection, so `&[:Grp[i]:]` is the exact
        overload (no `&C::f` ambiguity). Serves methods, static methods and free
        functions alike (sol2 registers all three as a table entry).
        @tparam Grp    the overload group (a static array of reflections).
        @tparam Target the sol2 usertype/table type to register onto.
        @tparam I      the group index pack.
        @param t the usertype or module table to register onto. */
    template <auto Grp, class Target, std::size_t... I>
    static void _register_named(Target& t, std::index_sequence<I...>) {
        constexpr const char* name{
            std::define_static_string(std::meta::identifier_of(Grp[0]))};
        if constexpr (sizeof...(I) == 1)
            t[name] = &[:Grp[0]:];
        else
            t[name] = ::sol::overload(&[:Grp[I]:]...);
    }

    /** Register operator group @a Grp under its Lua metamethod slot (a single
        callable, or a `sol::overload(…)` for several same-slot overloads).
        @tparam Grp    the operator overload group (a static array of reflections).
        @tparam Target the sol2 usertype type to register onto.
        @tparam I      the group index pack.
        @param t the usertype to register the metamethod onto. */
    template <auto Grp, class Target, std::size_t... I>
    static void _register_operator(Target& t, std::index_sequence<I...>) {
        constexpr auto slot{operator_mm(Grp[0]).fn};
        if constexpr (sizeof...(I) == 1)
            t[slot] = &[:Grp[0]:];
        else
            t[slot] = ::sol::overload(&[:Grp[I]:]...);
    }

  public:
    // --- caster oracle + emission primitives (the welder::rod contract) --

    /** `caster_oracle`: @a T converts without welder registering a usertype iff
        sol2 does not classify it as needs-registration. @see _needs_registration */
    template <class T>
    static constexpr bool has_native_caster = !_needs_registration<T>;

    /** Map a member operator to its Lua metamethod name (`nullptr` = not exposed). */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return operator_mm(op_fn).name;
    }

    // --- class binding ------------------------------------------------------

    /** Create the `sol::usertype<T>` handle (constructors installed from
        reflection; @a doc has no Lua runtime home and is ignored).

        The driver's @a Bases is welder's *nearest* welded-ancestor set; sol2 needs
        the full transitive closure (see _collect_welded_bases), so this recomputes
        it from @a T and ignores the passed-in @a Bases. @see _make_usertype */
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* /*doc*/,
                           std::index_sequence<I...> /*seq*/) {
        constexpr auto bases{_welded_bases_array<T>()};
        return _make_usertype<T, bases>(
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
            _register_named<grp>(ut, std::make_index_sequence<grp.size()>{});
        }
    }

    /** Bind static member function @a Fn as a class-table function (`T.name(…)`);
        overloads are grouped as in @ref add_method. */
    template <std::meta::info Fn>
    static void add_static_method(auto& ut) {
        if constexpr (is_overload_leader<method_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<method_overload_set, Fn, lang::lua>()};
            _register_named<grp>(ut, std::make_index_sequence<grp.size()>{});
        }
    }

    /** Bind member operator @a Fn under its Lua metamethod. The specific overload is
        spliced, so unary/binary forms never collide; several overloads mapping to
        the *same* metamethod slot are gathered into one `sol::overload(…)`. */
    template <std::meta::info Fn>
    static void add_operator(auto& ut) {
        if constexpr (is_overload_leader<operator_overload_set>(Fn, lang::lua)) {
            constexpr auto grp{overload_group<operator_overload_set, Fn, lang::lua>()};
            _register_operator<grp>(ut, std::make_index_sequence<grp.size()>{});
        }
    }

    // --- enum binding -------------------------------------------------------

    /** Create the enum's `Name = value` table on the module (@a doc ignored). */
    template <class E>
    static auto make_enum(module_type& m, const char* name, const char* /*doc*/) {
        ::sol::table values{::sol::state_view{m.lua_state()}.create_table()};
        m[name] = values;
        return _enum_binding<E>{values, m, std::is_scoped_enum_v<E>};
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
            _register_named<grp>(m, std::make_index_sequence<grp.size()>{});
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

static_assert(::welder::rod<rod>,
              "welder::rods::sol2::rod must satisfy welder::rod");

} // namespace welder::rods::sol2
