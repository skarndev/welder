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

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`).
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
      ≥ 5.3 (not on LuaJIT's 5.1 ABI); they are `#if`-gated. See @ref welder::rods::sol2::operator_mm.
    - **Constructors, all at once.** sol2 takes a type's whole constructor set in one
      `sol::constructors<…>` — exactly the shape of the driver's single
      @ref welder::rods::sol2::rod::add_constructors call (the participating
      constructor reflections plus the default/aggregate flags), turned into one
      assignment here.
    - **Enums are tables.** Lua has no enum type; a welded enum becomes a plain
      name→integer table (an unscoped enum's names are also mirrored onto the
      enclosing module, like C++).
    - **No runtime docstrings.** Lua has no `__doc__` slot, so `[[=welder::doc]]` /
      `returns` are not surfaced at runtime; their home is a generated LuaCATS
      (`---@meta`) stub, a separate emitter (not part of this binding backend).
    - **Namespace variables: snapshot or live.** A const/constexpr variable binds as
      a value snapshot at load time; a mutable one binds as a **live get/set** over
      the C++ global via a metatable proxy on the module table (`__index`/`__newindex`
      routing absent keys through per-variable getters/setters), so reads see the
      current value and writes flow back — matching the Python backends.

    @note Overloaded methods, static methods, free functions and operators: sol2
    stores one value per name (and per metamethod slot), and the DRIVER hands each
    name's participating overloads to the rod as one group — registered here as a
    single `sol::overload(…)`, dispatched among at call time. A same-named member
    in a derived class still hides the base's (C++ name-hiding), as it did before.
*/
#include <array>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

#include <welder/welder.hpp>           // welder::welder + rod contract + driver
#include <welder/rods/lua/sol2/metamethods.hpp> // operator -> sol2 metamethod map
#include <welder/bind_traits.hpp>      // aggregate_* helpers

#include <sol/sol.hpp>

namespace welder::inline v0::rods::sol2 {

// Note: the namespace is `welder::rods::sol2`; the library namespace is `::sol`. They do
// not collide, so `sol::` below refers to the library without an alias (unlike the
// Python backends, whose namespace shadowed the library's).

/** The alias `T(A...)` — a constructor call signature, built by `substitute`
    (a namespace-scope alias so it can be reflected as `^^ctor_sig`). */
template <class T, class... A>
using ctor_sig = T(A...);

/** The sol2 rod: a stateless policy type satisfying @ref welder::rod.

    Its public static members are the sol2 emission primitives welder's driver
    calls; the driver supplies all the reflection-derived decisions. Each implements
    the correspondingly-named hook of the @ref welder::rod contract (and
    @ref welder::caster_oracle) — every one carries a `@see` back to it, where the
    shared parameter and return-value semantics are documented once rather than
    repeated on each backend's mirror. The `protected` members below are sol2-specific
    implementation helpers (prefixed `_`), not part of the contract; the
    operator→metamethod map lives in `metamethods.hpp`.
*/
struct rod {
    static constexpr lang language{lang::lua}; /**< welder::lang::lua. */
    using module_type = ::sol::table;          /**< A Lua module is a table. */

    /** Per-module session: the deferred state for *live* namespace variables.

        A mutable variable is not stored on the module table directly; instead its
        `name → getter`/`name → setter` closures accumulate here, and `close_module`
        installs a `__index`/`__newindex` metatable proxy that routes reads/writes of
        those absent keys through them (see @ref add_variable). `getters`/`setters`
        stay nil (default-constructed) until the first mutable variable, so a module
        with only snapshots or types pays nothing. Const variables never reach here —
        they snapshot as plain table entries. */
    struct session {
        ::sol::table getters{}; /**< name → `fun(): value` (live reads). */
        ::sol::table setters{}; /**< name → `fun(value)` (live writes). */
        bool has_live{false};   /**< any mutable variable registered? */
    };

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

        Built from the pieces the DRIVER hands to add_constructors — the
        participating constructor reflections plus the default/aggregate flags —
        because sol2 wants the whole set in one assignment (aggregates ride C++26
        parenthesized aggregate init). Each signature is a `substitute`d, dealiased
        `ctor_sig<T, Params…>`.

        @tparam T          the class type.
        @tparam Ctors      the participating constructor reflections.
        @tparam HasDefault whether the default constructor is exposed.
        @tparam Aggregate  whether the synthesized field constructor is exposed.
        @return the constructor-signature reflections (may be empty: a type with no
                Lua-constructible form).
    */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate>
    static consteval std::vector<std::meta::info> _ctor_signatures() {
        std::vector<std::meta::info> sigs;
        auto sig = [](std::vector<std::meta::info> targs) {
            return std::meta::dealias(std::meta::substitute(^^ctor_sig, targs));
        };
        if (HasDefault)
            sigs.push_back(sig({^^T}));
        for (auto c : Ctors) {
            std::vector<std::meta::info> targs{^^T};
            for (auto p : std::meta::parameters_of(c))
                targs.push_back(std::meta::type_of(p));
            sigs.push_back(sig(targs));
        }
        if (Aggregate) {
            std::vector<std::meta::info> targs{^^T};
            for (auto fld : ::welder::detail::aggregate_fields<T>())
                targs.push_back(std::meta::type_of(fld));
            sigs.push_back(sig(targs));
        }
        return sigs;
    }

    /** `_ctor_signatures<T, …>()` as a fixed-size, splice-ready static array. */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate>
    static consteval auto _ctor_sigs_array() {
        constexpr std::size_t n{
            _ctor_signatures<T, Ctors, HasDefault, Aggregate>().size()};
        std::array<std::meta::info, n> out{};
        // Guard the fill: std::array<T, 0>::operator[] is not consteval, so it must
        // not be instantiated for a type with no exposed constructors.
        if constexpr (n != 0) {
            auto v{_ctor_signatures<T, Ctors, HasDefault, Aggregate>()};
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
        // sol::no_constructor suppresses sol2's implicit constructor; the real set
        // is installed afterwards by add_constructors (the driver's one call).
        if constexpr (sizeof...(I) == 0) {
            return ::sol::usertype<T>{m.new_usertype<T>(name, ::sol::no_constructor)};
        } else {
            return ::sol::usertype<T>{m.new_usertype<T>(
                name, ::sol::no_constructor, ::sol::base_classes,
                ::sol::bases<typename [:Bases[I]:]...>())};
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

    /** Register overload group @a Grp on target @a t under the (already name-styled)
        @a name — a single callable when unique, a `sol::overload(…)` when several.
        Each overload is spliced by its specific reflection, so `&[:Grp[i]:]` is the
        exact overload (no `&C::f` ambiguity). Serves methods, static methods and free
        functions alike (sol2 registers all three as a table entry). The caller passes
        the resolved name (via `welder::name_of`), since it varies by entity kind.
        @tparam Grp    the overload group (a static array of reflections).
        @tparam Target the sol2 usertype/table type to register onto.
        @tparam I      the group index pack.
        @param t    the usertype or module table to register onto.
        @param name the resolved target name (weld_as / style already applied). */
    template <auto Grp, class Target, std::size_t... I>
    static void _register_named(Target& t, const char* name,
                                std::index_sequence<I...>) {
        // sol2 owns a returned object structurally (a value → a Lua-owned
        // copy/move; a pointer/reference → a non-owning view), so a
        // [[=welder::return_policy]] has no runtime effect here — but a
        // self-contradictory one (a reference to a returned temporary) is still
        // rejected, uniformly with the Python rods.
        (::welder::validate_return_policy<Grp[I], language>(), ...);
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
        (::welder::validate_return_policy<Grp[I], language>(), ...);
        constexpr auto slot{operator_mm(Grp[0]).fn};
        if constexpr (sizeof...(I) == 1)
            t[slot] = &[:Grp[0]:];
        else
            t[slot] = ::sol::overload(&[:Grp[I]:]...);
    }

    // --- live namespace-variable proxy --------------------------------------

    /** Route a missing-key *read* on the module to a captured previous `__index`
        (function or table form), or nil when there was none. Used as the fallback
        when the requested key is not one of our live getters. */
    static ::sol::object _prev_index(::sol::this_state ts, const ::sol::object& prev,
                                     const ::sol::table& self, const ::sol::object& key) {
        if (prev.get_type() == ::sol::type::function)
            return ::sol::object{prev.as<::sol::function>()(self, key)};
        if (prev.get_type() == ::sol::type::table)
            return ::sol::object{prev.as<::sol::table>()[key]};
        return ::sol::make_object(ts, ::sol::lua_nil);
    }

    /** Route a missing-key *write* on the module to a captured previous
        `__newindex` (function or table form); with none, do a raw set — the normal
        new-key assignment, raw so it does not recurse back through this metamethod. */
    static void _prev_newindex(const ::sol::object& prev, ::sol::table self,
                               const ::sol::object& key, const ::sol::object& value) {
        if (prev.get_type() == ::sol::type::function) {
            prev.as<::sol::function>()(self, key, value);
            return;
        }
        if (prev.get_type() == ::sol::type::table) {
            prev.as<::sol::table>()[key] = value;
            return;
        }
        self.raw_set(key, value);
    }

    /** Install (or extend) the module table's metatable so absent keys in @a getters
        / @a setters read and write the underlying C++ globals live.

        Only *absent* keys trigger a metamethod, so this coexists with the module's
        ordinary entries (types, functions, snapshots, enum names) untouched — they
        are present keys and never reach here. Any metatable already on @a m (e.g. a
        prior live-variable install from another `weld_*` onto the same table) is
        chained: its `__index`/`__newindex` become the fallback, so earlier live
        variables keep working. A live key is deliberately never `rawset`, so it stays
        routed through the proxy across repeated reads and writes. */
    static void _install_live_variables(module_type& m, ::sol::table getters,
                                        ::sol::table setters) {
        lua_State* L{m.lua_state()};
        ::sol::state_view lua{L};
        // Fetch any existing metatable via the raw C API: reading it through a
        // checked sol::table would fail SOL_ALL_SAFETIES_ON when there is none (nil
        // is not a table). A pre-existing metatable (a prior live-variable install
        // onto the same table) is reused and chained; otherwise a fresh one is made.
        m.push();                                        // [module]
        const bool had_mt{lua_getmetatable(L, -1) != 0}; // [module, mt?]
        ::sol::table mt{had_mt ? ::sol::stack::pop<::sol::table>(L)
                               : lua.create_table()};
        lua_pop(L, 1);                                    // pop [module]
        // Capture any metamethods to chain (a state-bound nil when absent, so the
        // fallback helpers can safely query get_type()).
        ::sol::object prev_index{mt["__index"]};
        ::sol::object prev_newindex{mt["__newindex"]};

        mt["__index"] = [getters, prev_index](::sol::this_state ts, ::sol::table self,
                                              ::sol::object key) -> ::sol::object {
            ::sol::object g{getters[key]};
            if (g.get_type() == ::sol::type::function)
                return ::sol::object{g.as<::sol::function>()()}; // live read of the C++ global
            return _prev_index(ts, prev_index, self, key);
        };
        mt["__newindex"] = [setters, prev_newindex](::sol::table self, ::sol::object key,
                                                    ::sol::object value) {
            ::sol::object s{setters[key]};
            if (s.get_type() == ::sol::type::function) {
                s.as<::sol::function>()(value); // live write through to the C++ global
                return;                         // no rawset: the key stays live
            }
            _prev_newindex(prev_newindex, self, key, value);
        };
        m[::sol::metatable_key] = mt;
    }

  public:
    // --- caster oracle + emission primitives (the welder::rod contract) --

    /** The class / enum handles the per-class / per-enum hooks operate on — exactly
        what `make_class` / `make_enum` return. Named as associated types so the @ref
        welder::rod concept can shape-check the per-handle hooks against them (`sol2`'s
        `_enum_binding` is an internal type, exposed here only through this alias). */
    template <class T> using class_handle_type = ::sol::usertype<T>;
    template <class E> using enum_handle_type = _enum_binding<E>;

    /** `caster_oracle`: @a T converts without welder registering a usertype iff
        sol2 does not classify it as needs-registration.
        @tparam T the type to classify.
        @see _needs_registration @see welder::caster_oracle */
    template <class T>
    static constexpr bool has_native_caster = !_needs_registration<T>;

    /** Map a member operator to its Lua metamethod name (`nullptr` = not exposed).
        @see welder::rod */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return operator_mm(op_fn).name;
    }

    // --- class binding ------------------------------------------------------

    /** Create the `sol::usertype<T>` handle (constructors are installed by the
        driver's subsequent add_constructors call; @a doc has no Lua runtime home
        and is ignored).

        The driver's @a Bases is welder's *nearest* welded-ancestor set; sol2 needs
        the full transitive closure (see _collect_welded_bases), so this recomputes
        it from @a T and ignores the passed-in @a Bases.
        @see _make_usertype @see welder::rod */
    template <class T, auto Bases, std::size_t... I>
    static auto make_class(module_type& m, const char* name, const char* /*doc*/,
                           std::index_sequence<I...> /*seq*/) {
        constexpr auto bases{_welded_bases_array<T>()};
        return _make_usertype<T, bases>(
            m, name, std::make_index_sequence<bases.size()>{});
    }

    /** Install @a T's whole constructor set — exactly what sol2 wants (one
        `sol::constructors<…>` assignment covering the call form `T(…)` and the
        idiomatic `T.new(…)`), built from the driver-passed pieces.
        @see _ctor_signatures @see _set_constructors @see welder::rod */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate>
    static void add_constructors(::sol::usertype<T>& ut) {
        constexpr auto sigs{_ctor_sigs_array<T, Ctors, HasDefault, Aggregate>()};
        if constexpr (sigs.size() != 0)
            _set_constructors<T, sigs>(ut, std::make_index_sequence<sigs.size()>{});
    }

    /** Bind data member @a Mem as a usertype property.

        A mutable member becomes read/write; a const member is wrapped in
        `sol::readonly` (its setter would not compile). Lua has no property
        docstring, so a `[[=welder::doc]]` on the member is not surfaced at runtime
        (it belongs in a generated stub). @see welder::rod */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(auto& ut) {
        constexpr const char* name{
            ::welder::name_of<Mem, language, Style, ::welder::ent_kind::field>()};
        if constexpr (std::meta::is_const_type(std::meta::type_of(Mem)))
            ut[name] = ::sol::readonly(&[:Mem:]);
        else
            ut[name] = &[:Mem:];
    }

    /** Bind method overload group @a Fns as one method (`obj:name(…)`) — a single
        callable when unique, one `sol::overload(…)` when several (sol2 stores one
        value per table key). The name resolves from `Fns[0]`. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_method(auto& ut) {
        _register_named<Fns>(
            ut,
            ::welder::name_of<Fns[0], language, Style, ::welder::ent_kind::method>(),
            std::make_index_sequence<Fns.size()>{});
    }

    /** Bind static-method overload group @a Fns as a class-table function
        (`T.name(…)`), grouped as in @ref add_method. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_static_method(auto& ut) {
        _register_named<Fns>(
            ut,
            ::welder::name_of<Fns[0], language, Style,
                              ::welder::ent_kind::static_method>(),
            std::make_index_sequence<Fns.size()>{});
    }

    /** Bind operator overload group @a Fns under its Lua metamethod (one operator +
        arity per group; each overload is spliced, several same-slot overloads become
        one `sol::overload(…)`). @see welder::rod */
    template <auto Fns>
    static void add_operator(auto& ut) {
        _register_operator<Fns>(ut, std::make_index_sequence<Fns.size()>{});
    }

    // --- enum binding -------------------------------------------------------

    /** Create the enum's `Name = value` table on the module (@a doc ignored).
        @see welder::rod */
    template <class E>
    static auto make_enum(module_type& m, const char* name, const char* /*doc*/) {
        ::sol::table values{::sol::state_view{m.lua_state()}.create_table()};
        m[name] = values;
        return _enum_binding<E>{values, m, std::is_scoped_enum_v<E>};
    }

    /** Add enumerator @a Enum (as its underlying integer). An unscoped enum's
        enumerator is also mirrored onto the enclosing module, unqualified — the
        driver's `finish_enum` role, done incrementally here since the handle
        carries the parent. @see welder::rod */
    template <std::meta::info Enum, class Style = ::welder::naming::none>
    static void add_enumerator(auto& e) {
        constexpr const char* name{
            ::welder::name_of<Enum, language, Style, ::welder::ent_kind::enumerator>()};
        const lua_Integer value{static_cast<lua_Integer>(std::to_underlying([:Enum:]))};
        e.values[name] = value;
        if (!e.scoped)
            e.parent[name] = value;
    }

    /** No whole-enum finalizer needed (unscoped export is done per-enumerator).
        @see welder::rod */
    template <class /*E*/>
    static void finish_enum(auto&) {}

    // --- namespace / module binding -----------------------------------------

    /** Open a per-module session. The live-variable registry tables are created
        lazily (on the first mutable variable), so a plain type/function module never
        allocates them. @see welder::rod */
    static session open_module(module_type&) { return {}; }

    /** No runtime module docstring in Lua (its home is a generated stub).
        @see welder::rod */
    static void set_module_doc(module_type&, const char*) {}

    /** Bind free-function overload group @a Fns as one module-level function
        (a single callable, or one `sol::overload(…)`; name from `Fns[0]`).

        A non-null @a name overrides the resolved name (including any `weld_as`), used
        verbatim; `nullptr` falls back to the styled/`weld_as` name.
        @return the bound function object (the table entry) — the handle for further
                hand-registration. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static sol::object add_function(module_type& m, const char* name = nullptr) {
        const char* fn_name{::welder::name_of_or<
            Fns[0], language, Style, ::welder::ent_kind::function>(name)};
        _register_named<Fns>(m, fn_name, std::make_index_sequence<Fns.size()>{});
        return m.get<sol::object>(fn_name);
    }

    /** Bind namespace variable @a Var onto the module.

        A const/constexpr variable becomes a **value snapshot** — a plain table entry
        frozen at load time (it can never change, so the snapshot is exact). A mutable
        variable becomes a **live get/set** over the C++ global: its getter/setter
        closures are accumulated in @a s and `close_module` wires them into a metatable
        proxy, so Lua reads see the current C++ value and Lua writes flow back to it
        (matching the Python backends). To keep the key routed through the proxy it is
        deliberately *not* stored on the table (a present key would bypass `__index`).

        A non-null @a name overrides the resolved name (including any `weld_as`), used
        verbatim; `nullptr` falls back to the styled/`weld_as` name. @see welder::rod */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, session& s, const char* name = nullptr) {
        const char* key{::welder::name_of_or<Var, language, Style,
                                             ::welder::ent_kind::variable>(name)};
        if constexpr (std::meta::is_const_type(std::meta::type_of(Var))) {
            m[key] = [:Var:]; // immutable: a value snapshot at load time
        } else {
            if (!s.has_live) {
                ::sol::state_view lua{m.lua_state()};
                s.getters = lua.create_table();
                s.setters = lua.create_table();
                s.has_live = true;
            }
            s.getters[key] = [] { return [:Var:]; };
            s.setters[key] = [](typename [:std::meta::type_of(Var):] v) {
                [:Var:] = v;
            };
        }
    }

    /** Create a submodule table named @a name under @a m. @see welder::rod */
    static module_type add_submodule(module_type& m, const char* name) {
        ::sol::table sub{::sol::state_view{m.lua_state()}.create_table()};
        m[name] = sub;
        return sub;
    }

    /** Close the session: install the live-variable metatable proxy, if any
        mutable variable was welded onto @a m. @see welder::rod */
    static void close_module(module_type& m, session& s) {
        if (s.has_live)
            _install_live_variables(m, s.getters, s.setters);
    }
};

static_assert(::welder::rod<rod>,
              "welder::rods::sol2::rod must satisfy welder::rod");

} // namespace welder::rods::sol2
