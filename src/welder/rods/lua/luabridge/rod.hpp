#pragma once
/** @file
    welder LuaBridge3 Lua rod (header-only).

    A second Lua runtime rod alongside sol2: it implements welder's rod contract
    (`<welder/welder.hpp>`) for [LuaBridge3](https://github.com/kunitoki/LuaBridge3)
    and hands the traversal/resolution off to welder's generic driver. All the
    language-agnostic work — deciding which members bind, gating bindability, walking
    bases and namespaces — lives in the core; only the LuaBridge3 emission primitives
    are here. Like sol2 it targets a **loadable Lua C module** entered through
    `luaopen_<name>`, but it lays the bindings down with LuaBridge3's fluent
    registrar instead of sol2's usertypes.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`). This
    header exposes exactly one thing to plug into `welder::welder`: the rod type
    `welder::rods::luabridge::rod`.
    @code
    #include <LuaBridge/LuaBridge.h>
    #include <welder/rods/lua/luabridge/rod.hpp>
    extern "C" int luaopen_mymod(lua_State* L) {
        using rod = welder::rods::luabridge::rod;
        rod::module_type m{L, {"mymod"}};
        welder::welder<rod>::weld_type<MyType>(m);
        lua_getglobal(L, "mymod");        // the populated module table
        lua_pushnil(L); lua_setglobal(L, "mymod"); // keep _G clean
        return 1;
    }
    @endcode
    or, more simply, via the backend-agnostic entry macro (include this directory's
    `module.hpp` instead):
    @code
    WELDER_MODULE(mymod, luabridge) {}   // welds namespace ^^mymod into luaopen_mymod
    @endcode

    ## How this differs from the sol2 rod (both target Lua)

    LuaBridge3 is a **move-based fluent registrar**: `beginClass` / `beginNamespace`
    move-consume their parent, and there may be only one "active" registrar at a
    time. welder's driver instead holds a stable module handle and mutates class /
    module handles across many separate `add_*` calls. The two are bridged with a
    **re-open-by-path** model: @ref welder::rods::luabridge::rod::module_scope is a light, copyable
    `{lua_State*, namespace path}`, and every emission primitive re-walks
    `getGlobalNamespace(L).beginNamespace(path…)` in a single chained expression and
    lets it unwind cleanly. `beginNamespace` reuses an existing namespace table (no
    wipe) and `beginClass<T>` re-opens a class preserving prior registrations, so the
    repeated open/close is correct; it costs a few extra table lookups per member at
    load time only. Everything else mirrors sol2:
    - **Metamethods**, not dunders — the same asymmetric Lua set as sol2 (see @ref
      welder::rods::lua::lua_metamethod_name), registered by their `__name` string via
      `addFunction("__add", …)`.
    - **Constructors, all at once** — LuaBridge3 takes the whole set in one
      `addConstructor<Sig…>()`, so this rod gathers them from reflection in
      @ref welder::rods::luabridge::rod::make_class and the driver's per-constructor
      hooks are no-ops (aggregates ride
      C++26 parenthesized init).
    - **Multiple / virtual inheritance** binds (unlike nanobind): LuaBridge3's
      `deriveClass<T, Base…>` records each base with a computed cast offset and its
      `__index` chains transitively, so the core's *nearest* welded-ancestor set is
      enough (unlike sol2, which needed the full closure).
    - **Enums are tables** — a welded enum becomes a nested namespace of integer
      values (an unscoped enum's names are also mirrored onto the enclosing module).
    - **No runtime docstrings** — `doc`/`returns` are ignored (their home is the
      LuaCATS stub).
    - **Namespace variables: snapshot or live** — a const variable is copied in as a
      value; a mutable one becomes a native `addProperty` get/set over the C++ global
      (LuaBridge3 has first-class properties, so no metatable proxy is needed).

    @note Overloaded methods, static methods, free functions and operators arrive
    from the DRIVER as whole groups and register as one variadic
    `addFunction(name, f1, f2, …)` — LuaBridge3 dispatches among them by arity/type
    at call time.
*/
#include <array>
#include <cstddef>
#include <meta>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <welder/welder.hpp>              // welder::welder + rod contract + driver
#include <welder/rods/lua/metamethods.hpp> // shared operator -> Lua __name map
#include <welder/bind_traits.hpp>         // aggregate_* helpers

// LuaBridge3 requires the Lua headers to be visible first (its Config.h hard-errors
// otherwise). Pull them in here so a binding TU only needs this rod header, the way
// the sol2 rod gets Lua transitively through <sol/sol.hpp>. lua.hpp is the C++
// convenience wrapper every PUC-Lua / LuaJIT distribution ships.
#include <lua.hpp>

#include <LuaBridge/LuaBridge.h>

// Make every C++ enum convert to/from a Lua integer. By default LuaBridge3 treats
// an unspecialized type as userdata, so an enum-typed function parameter/return
// would demand a registered userdata; welder instead binds an enum as a table of
// integer values (see make_enum), so its values cross the boundary as integers.
// This blanket specialization — the generalization of LuaBridge3's opt-in
// `Stack<E> = Enum<E>` — makes that work for any welded enum without per-enum glue.
namespace luabridge {
template <class E>
struct Stack<E, std::enable_if_t<std::is_enum_v<E>>> : Enum<E> {};
} // namespace luabridge

namespace welder::inline v0::rods::luabridge {

// The welder rod namespace `welder::rods::luabridge` would shadow the library's
// `::luabridge`, so alias it (as the Python rods alias pybind11/nanobind) and use
// `lb::` throughout.
namespace lb = ::luabridge;

using ::welder::rods::lua::lua_metamethod_name;

/** The alias `void(A...)` — a LuaBridge3 constructor signature (a function type
    whose parameters are the constructor arguments; the return type is ignored).
    A namespace-scope alias so it can be reflected/substituted as `^^ctor_sig`. */
template <class... A>
using ctor_sig = void(A...);

/** A factory that constructs `T` from the constructor arguments (works for normal
    constructors and, via C++26 parenthesized init, aggregates). Registered as the
    idiomatic `T.new(…)` static function — `addConstructor` alone only sets the call
    form `T(…)`, but welder's Lua rods (like sol2) expose both. A namespace-scope
    template so `&make_object<T, A...>` is a plain function pointer to splice. */
template <class T, class... A>
T make_object(A... args) {
    return T(std::move(args)...);
}

/** The LuaBridge3 rod: a stateless policy type satisfying @ref welder::rod.

    Its public static members are the emission primitives welder's driver calls; the
    driver supplies all the reflection-derived decisions. Each implements the
    correspondingly-named hook of the @ref welder::rod contract (and
    @ref welder::caster_oracle) — every one carries a `@see` back to it, where the
    shared parameter and return-value semantics are documented once rather than
    repeated on each backend's mirror. The `protected` members are LuaBridge3
    implementation helpers (prefixed `_`); the operator→metamethod-name map is shared
    in `<welder/rods/lua/metamethods.hpp>`. */
struct rod {
    static constexpr lang language{lang::lua}; /**< welder::lang::lua. */

    /** A copyable handle to a welded module (or submodule) table: the borrowed Lua
        state plus the namespace path from the global table.

        LuaBridge3's registrar is move-based and single-active, which does not match the
        driver's stable-handle model, so instead of holding a live registrar this names
        *where* the module lives and every emission primitive re-opens it via
        `getGlobalNamespace(L).beginNamespace(path…)`. @see the file overview. */
    struct module_scope {
        lua_State* L{nullptr};             /**< The borrowed Lua state. */
        std::vector<std::string> path{};   /**< Namespace segments under `_G`. */
    };
    using module_type = module_scope;          /**< A Lua module is a named table. */

    /** Per-module session — unused: LuaBridge3 registers namespace variables as
        live properties eagerly (no deferred batch is needed, unlike sol2's live-
        variable proxy), so this is empty and `open_module`/`close_module` are
        no-ops. */
    struct session {};

    /** The class handle threaded from `make_class` to the `add_*` hooks: enough to
        re-open the class (its module, its Lua name) plus the C++ type (via `type`,
        recovered by the per-member hooks from `decltype`). */
    template <class T>
    struct class_handle {
        using type = T;      /**< The C++ class type. */
        module_scope mod;    /**< The enclosing module. */
        std::string name;    /**< The class's Lua name. */
    };

    /** The enum handle threaded from `make_enum` to `add_enumerator`: the enclosing
        module, the enum's Lua name (a nested namespace of values) and whether it is
        scoped (an unscoped enum also mirrors its names onto the module). */
    struct enum_handle {
        module_scope mod;
        std::string name;
        bool scoped;
    };

    /** The class / enum handles the per-class / per-enum hooks receive — exactly what
        `make_class` / `make_enum` return (the enum handle carries no type parameter).
        Named as associated types so the @ref welder::rod concept can shape-check the
        per-handle hooks against them. */
    template <class T> using class_handle_type = class_handle<T>;
    template <class> using enum_handle_type = enum_handle;

  protected:
    // --- implementation helpers (not part of the welder::rod contract) --

    /** Whether LuaBridge3 can only convert @a T via *runtime class registration*.

        LuaBridge3 classifies any type without a dedicated `Stack<T>` specialization
        as userdata — a program-defined class it can convert only once registered;
        scalars, `bool`, strings and the like have specializations and convert
        natively. `luabridge::detail::IsUserdata<T>` reads exactly that. This is the
        one bindability fact welder's core cannot know; it drives `has_native_caster`.

        Enums are forced into the needs-registration bucket (like sol2): welder binds
        a welded enum as its table of values and gates enum-typed members on it. The
        STL-wrapper recursion in `bindable.hpp` is shared, so containers/optional/…
        of a native/welded type never reach here. Conservative: it reports whether
        @a T *needs* registration, not whether one exists — the `trust_bindable`
        hatch resolves the false positive for a hand-registered type. */
    template <class T>
    static constexpr bool _needs_registration =
        std::is_enum_v<std::remove_cvref_t<T>> ||
        lb::detail::IsUserdata<std::remove_cvref_t<T>>::value;

    /** The C++ type behind a `class_handle<T>&` (deduced from the driver's `auto&`). */
    template <class H>
    using _class_type = typename std::remove_cvref_t<H>::type;

    /** Open the module's namespace chain from the global namespace, returning the
        innermost `luabridge::Namespace`. The returned registrar owns the whole
        chain's Lua stack, so letting it (or a class/temporary derived from it)
        destruct unwinds everything cleanly — every emission primitive opens like
        this, registers in one chained expression, and lets it go. */
    static lb::Namespace _open_namespace(const module_scope& m) {
        lb::Namespace ns{lb::getGlobalNamespace(m.L)};
        for (const auto& seg : m.path)
            ns = ns.beginNamespace(seg.c_str());
        return ns;
    }

    /** Re-open class @a T under @a name in module @a m, returning the live
        `luabridge::Namespace::Class<T>` (created by `make_class`; here re-opened to
        continue registration). Destroying the result unwinds the class + namespace +
        `_G`. Returned by `auto` because the class type is a private nested type of
        `luabridge::Namespace`. */
    template <class T>
    static auto _open_class(const module_scope& m, const char* name) {
        return _open_namespace(m).template beginClass<T>(name);
    }

    /** The set of constructor argument lists to expose for @a T, built from the
        pieces the DRIVER hands to add_constructors — the participating constructor
        reflections plus the default/aggregate flags (aggregates ride C++26
        parenthesized aggregate init) — gathered into one place because LuaBridge3
        wants the whole set at once. */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate>
    static consteval std::vector<std::vector<std::meta::info>> _ctor_arg_lists() {
        std::vector<std::vector<std::meta::info>> lists;
        if (HasDefault)
            lists.push_back({}); // ()
        for (auto c : Ctors) {
            std::vector<std::meta::info> args;
            for (auto p : std::meta::parameters_of(c))
                args.push_back(std::meta::type_of(p));
            lists.push_back(args);
        }
        if (Aggregate) {
            std::vector<std::meta::info> args;
            for (auto fld : ::welder::detail::aggregate_fields<T>())
                args.push_back(std::meta::type_of(fld));
            lists.push_back(args);
        }
        return lists;
    }

    /** The `void(A...)` constructor-signature reflections (for `addConstructor`) as a
        fixed-size, splice-ready static array. */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate>
    static consteval auto _ctor_sigs_array() {
        constexpr std::size_t n{
            _ctor_arg_lists<T, Ctors, HasDefault, Aggregate>().size()};
        std::array<std::meta::info, n> out{};
        if constexpr (n != 0) {
            auto lists{_ctor_arg_lists<T, Ctors, HasDefault, Aggregate>()};
            for (std::size_t i{0}; i < n; ++i)
                out[i] = std::meta::dealias(std::meta::substitute(^^ctor_sig, lists[i]));
        }
        return out;
    }

    /** The `make_object<T, A...>` factory-function reflections (for the `.new` static
        function) as a fixed-size, splice-ready static array. */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate>
    static consteval auto _factory_array() {
        constexpr std::size_t n{
            _ctor_arg_lists<T, Ctors, HasDefault, Aggregate>().size()};
        std::array<std::meta::info, n> out{};
        if constexpr (n != 0) {
            auto lists{_ctor_arg_lists<T, Ctors, HasDefault, Aggregate>()};
            for (std::size_t i{0}; i < n; ++i) {
                std::vector<std::meta::info> targs{^^T};
                for (auto a : lists[i])
                    targs.push_back(a);
                out[i] = std::meta::substitute(^^make_object, targs);
            }
        }
        return out;
    }

    /** Register the whole constructor set on the (live) class @a cls: both the call
        form `T(…)` (`addConstructor`, which sets `__call`) and the idiomatic `T.new(…)`
        (a variadic `addStaticFunction` over the `make_object` factories) — matching
        the two forms sol2 exposes. @a Cls is the deduced `Class<T>` handle type. */
    template <auto Sigs, auto Factories, class Cls, std::size_t... I>
    static void _add_constructors(Cls& cls, std::index_sequence<I...>) {
        cls.template addConstructor<typename [:Sigs[I]:]...>();
        cls.addStaticFunction("new", &[:Factories[I]:]...);
    }

    /** Create the class registration with its native (welded nearest-ancestor)
        bases, in one chained expression that then unwinds (constructors are
        installed by the driver's subsequent add_constructors call, which re-opens
        the class by path). */
    template <class T, auto Bases, std::size_t... I>
    static void _make_class(const module_scope& m, const char* name,
                            std::index_sequence<I...>) {
        if constexpr (sizeof...(I) == 0) {
            auto cls{_open_namespace(m).template beginClass<T>(name)};
        } else {
            auto cls{_open_namespace(m)
                         .template deriveClass<T, typename [:Bases[I]:]...>(name)};
        }
    }

    /** Register overload group @a Grp on target @a t (a live class or namespace)
        under @a name via LuaBridge3's variadic `addFunction` — one callable when
        unique, several dispatched by arity/type when overloaded. Each overload is
        spliced by its exact reflection. Serves methods, free functions and operators
        (all register through `addFunction`; an operator's @a name is its `__slot`). */
    template <auto Grp, class Target, std::size_t... I>
    static void _add_function(Target& t, const char* name,
                              std::index_sequence<I...>) {
        // LuaBridge3 owns a returned object structurally (a value → a Lua-owned
        // copy; a pointer/reference → a non-owning view), so a
        // [[=welder::return_policy]] has no runtime effect here — but a
        // self-contradictory one (a reference to a returned temporary) is still
        // rejected, uniformly with the Python rods.
        (::welder::validate_return_policy<Grp[I], language>(), ...);
        t.addFunction(name, &[:Grp[I]:]...);
    }

    /** As @ref _add_function, for a class's static methods (`addStaticFunction`). */
    template <auto Grp, class Target, std::size_t... I>
    static void _add_static_function(Target& t, const char* name,
                                     std::index_sequence<I...>) {
        (::welder::validate_return_policy<Grp[I], language>(), ...);
        t.addStaticFunction(name, &[:Grp[I]:]...);
    }

  public:
    // --- caster oracle + emission primitives (the welder::rod contract) --

    /** `caster_oracle`: @a T converts without welder registering a class iff
        LuaBridge3 does not classify it as needs-registration.
        @tparam T the type to classify.
        @see _needs_registration @see welder::caster_oracle */
    template <class T>
    static constexpr bool has_native_caster = !_needs_registration<T>;

    /** Map a member operator to its Lua metamethod name (`nullptr` = not exposed).
        Shared with the sol2 rod. @see welder::rod */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return lua_metamethod_name(op_fn);
    }

    // --- class binding ------------------------------------------------------

    /** Register class @a T (with its native bases and constructors) and return a
        re-openable handle. @a Bases is the core's *nearest* welded-ancestor set,
        which suffices for LuaBridge3's transitive `__index` chaining. @a doc has no
        Lua runtime home and is ignored. @see welder::rod */
    template <class T, auto Bases, std::size_t... I>
    static class_handle<T> make_class(module_type& m, const char* name,
                                      const char* /*doc*/,
                                      std::index_sequence<I...> /*seq*/) {
        _make_class<T, Bases>(m, name, std::make_index_sequence<Bases.size()>{});
        return class_handle<T>{m, std::string{name}};
    }

    /** Register @a T's whole constructor set — exactly what LuaBridge3 wants (one
        variadic `addConstructor` for the call form `T(…)` plus the `.new` factory
        set) — built from the driver-passed pieces on the re-opened class.
        @see _ctor_arg_lists @see _add_constructors @see welder::rod */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate>
    static void add_constructors(auto& h) {
        constexpr auto sigs{_ctor_sigs_array<T, Ctors, HasDefault, Aggregate>()};
        constexpr auto factories{_factory_array<T, Ctors, HasDefault, Aggregate>()};
        if constexpr (sigs.size() != 0) {
            auto cls{_open_class<T>(h.mod, h.name.c_str())};
            _add_constructors<sigs, factories>(
                cls, std::make_index_sequence<sigs.size()>{});
        }
    }

    /** Bind data member @a Mem as a class property (read-only if const, else
        read/write). Lua has no property docstring, so a `[[=welder::doc]]` on the
        member is not surfaced at runtime (it belongs in a generated stub).

        The member pointer is `static_cast` to `Field T::*`: a flattened base member
        splices as `Field Base::*`, which LuaBridge3's `addProperty` template
        deduction (fixed on the class `T`) will not up-convert on its own — the cast
        is the derived→base pointer-to-member standard conversion, a no-op for @a T's
        own members. @see welder::rod */
    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(auto& h) {
        using T = _class_type<decltype(h)>;
        constexpr const char* name{
            ::welder::name_of<Mem, language, Style, ::welder::ent_kind::field>()};
        auto cls{_open_class<T>(h.mod, h.name.c_str())};
        if constexpr (!std::meta::is_public(Mem)) {
            // A protected member (admitted under policy::weld_protected) binds
            // as a getter/setter property over welder::detail::field_access —
            // gcc-16 rejects the dependent `&[:Mem:]` for protected data (see
            // field_access).
            using fa = ::welder::detail::field_access<Mem>;
            if constexpr (std::meta::is_const_type(std::meta::type_of(Mem)))
                cls.addProperty(name, &fa::get);
            else
                cls.addProperty(name, &fa::get, &fa::set);
        } else {
            using Field = typename [:std::meta::type_of(Mem):];
            constexpr Field T::* mp{&[:Mem:]};
            if constexpr (std::meta::is_const_type(std::meta::type_of(Mem)))
                cls.addProperty(name, mp);      // read-only (const member)
            else
                cls.addProperty(name, mp, mp);  // read/write
        }
    }

    /** Bind method overload group @a Fns as one method (`obj:name(…)`) via a single
        variadic `addFunction` — LuaBridge3 dispatches by arity/type at call time.
        The name resolves from `Fns[0]`. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_method(auto& h) {
        using T = _class_type<decltype(h)>;
        auto cls{_open_class<T>(h.mod, h.name.c_str())};
        _add_function<Fns>(
            cls,
            ::welder::name_of<Fns[0], language, Style, ::welder::ent_kind::method>(),
            std::make_index_sequence<Fns.size()>{});
    }

    /** Bind static-method overload group @a Fns as a class-table function
        (`T.name(…)`), grouped as in @ref add_method. @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_static_method(auto& h) {
        using T = _class_type<decltype(h)>;
        auto cls{_open_class<T>(h.mod, h.name.c_str())};
        _add_static_function<Fns>(
            cls,
            ::welder::name_of<Fns[0], language, Style,
                              ::welder::ent_kind::static_method>(),
            std::make_index_sequence<Fns.size()>{});
    }

    /** Bind member operator @a Fn under its Lua metamethod `__name`. The specific
        overload is spliced (unary/binary forms never collide); several overloads
        mapping to the same slot are gathered into one variadic `addFunction`.

        `operator[]` is special: it maps to `__index`, but LuaBridge3 *reserves*
        `__index` for its own member/property resolution and forbids replacing it (an
        override would shadow every field and method). Instead it is registered as the
        `addIndexMetaMethod` **fallback**. That fallback is consulted *first* on every
        index, and must return `nil` for keys it does not handle so normal member /
        property lookup proceeds — so the adapter tries to convert the Lua key to the
        operator's index type, returns the subscript result on success, and a nil
        `LuaRef` otherwise (letting `obj.field` / `obj:method()` resolve as usual, the
        "coexists with member access" semantics sol2 gets from `__index` being a last
        resort).

        A numeric-index operator (`operator[](int)`, the common case) needs one extra
        care: LuaBridge3's index metamethod runs `lua_tostring` on the key first, which
        mutates a *number* key into a string in place, so `obj[0]` reaches the fallback
        as `"0"` and a strict `Stack<int>` cast would reject it. For an arithmetic index
        type the adapter therefore coerces the key with `lua_tonumberx` (which accepts a
        numeric string), and only treats a genuinely non-numeric key as a member miss.
        @see welder::rod */
    template <auto Fns>
    static void add_operator(auto& h) {
        using T = _class_type<decltype(h)>;
        constexpr auto Fn{Fns[0]};
        {
            auto cls{_open_class<T>(h.mod, h.name.c_str())};
            if constexpr (std::meta::operator_of(Fn) ==
                          std::meta::operators::op_square_brackets) {
                using Key = std::remove_cvref_t<typename [:std::meta::type_of(
                    std::meta::parameters_of(Fn)[0]):]>;
                cls.addIndexMetaMethod(
                    +[](T& self, const lb::LuaRef& key, lua_State* L) -> lb::LuaRef {
                        if constexpr (std::is_arithmetic_v<Key>) {
                            key.push(L); // may be a stringified number (see above)
                            int is_num{0};
                            const lua_Number n{lua_tonumberx(L, -1, &is_num)};
                            lua_pop(L, 1);
                            if (!is_num)
                                return lb::LuaRef(L); // not a subscript key
                            return lb::LuaRef(L, self[static_cast<Key>(n)]);
                        } else {
                            if (auto k = key.template cast<Key>())
                                return lb::LuaRef(L, self[*k]);
                            return lb::LuaRef(L); // nil: fall through to member lookup
                        }
                    });
            } else {
                constexpr const char* slot{lua_metamethod_name(Fn)};
                _add_function<Fns>(cls, slot, std::make_index_sequence<Fns.size()>{});
            }
        }
    }

    // --- enum binding -------------------------------------------------------

    /** Create the enum's nested namespace (a `Name = value` table) on the module
        (@a doc ignored) and return a re-openable handle. @see welder::rod */
    template <class E>
    static enum_handle make_enum(module_type& m, const char* name,
                                 const char* /*doc*/) {
        _open_namespace(m).beginNamespace(name); // create (empty), then unwind
        return enum_handle{m, std::string{name}, std::is_scoped_enum_v<E>};
    }

    /** Add enumerator @a Enum (as its underlying integer) to the enum's table. An
        unscoped enum's enumerator is also mirrored onto the enclosing module,
        unqualified — the driver's `finish_enum` role, done incrementally here since
        the handle carries the module. @see welder::rod */
    template <std::meta::info Enum, class Style = ::welder::naming::none>
    static void add_enumerator(enum_handle& e) {
        constexpr const char* name{
            ::welder::name_of<Enum, language, Style, ::welder::ent_kind::enumerator>()};
        const lua_Integer value{static_cast<lua_Integer>(std::to_underlying([:Enum:]))};
        _open_namespace(e.mod).beginNamespace(e.name.c_str()).addVariable(name, value);
        if (!e.scoped)
            _open_namespace(e.mod).addVariable(name, value);
    }

    /** No whole-enum finalizer needed (unscoped export is done per-enumerator).
        @see welder::rod */
    template <class /*E*/>
    static void finish_enum(auto&) {}

    // --- namespace / module binding -----------------------------------------

    /** Open a per-module session (unused; see @ref session). @see welder::rod */
    static session open_module(module_type&) { return {}; }

    /** No runtime module docstring in Lua (its home is a generated stub).
        @see welder::rod */
    static void set_module_doc(module_type&, const char*) {}

    /** Bind free-function overload group @a Fns as one module-level function (a
        single variadic `addFunction`; name from `Fns[0]`). A non-null @a name
        overrides the resolved name (including any `weld_as`), used verbatim.
        @see welder::rod */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_function(module_type& m, const char* name = nullptr) {
        lb::Namespace ns{_open_namespace(m)};
        _add_function<Fns>(
            ns,
            ::welder::name_of_or<Fns[0], language, Style,
                                 ::welder::ent_kind::function>(name),
            std::make_index_sequence<Fns.size()>{});
    }

    /** Bind namespace variable @a Var onto the module.

        A const/constexpr variable is copied in as a **value snapshot** (frozen at
        load time). A mutable variable becomes a **live get/set** over the C++ global
        via LuaBridge3's native property support, so Lua reads see the current value
        and writes flow back. A non-null @a name overrides the resolved name.
        @see welder::rod */
    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, session& /*s*/,
                             const char* name = nullptr) {
        const char* key{::welder::name_of_or<Var, language, Style,
                                             ::welder::ent_kind::variable>(name)};
        lb::Namespace ns{_open_namespace(m)};
        if constexpr (std::meta::is_const_type(std::meta::type_of(Var))) {
            ns.addVariable(key, [:Var:]); // immutable: a value snapshot at load time
        } else {
            using VT = typename [:std::meta::type_of(Var):];
            ns.addProperty(
                key, +[]() -> VT { return [:Var:]; }, +[](VT v) { [:Var:] = v; });
        }
    }

    /** Create a submodule (nested namespace) named @a name under @a m. @see welder::rod */
    static module_type add_submodule(module_type& m, const char* name) {
        _open_namespace(m).beginNamespace(name); // create, then unwind
        module_type sub{m};
        sub.path.emplace_back(name);
        return sub;
    }

    /** Close the session (no-op; see @ref session). @see welder::rod */
    static void close_module(module_type&, session&) {}
};

static_assert(::welder::rod<rod>,
              "welder::rods::luabridge::rod must satisfy welder::rod");

} // namespace welder::rods::luabridge