#pragma once
#include <array>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

#include <welder/bind_traits.hpp> // what-binds selection layer
#include <welder/bindable.hpp>    // bindability gate + caster_oracle
#include <welder/concepts.hpp>    // the welder::rod contract
#include <welder/doc.hpp>         // doc_of (class / namespace docstrings)
#include <welder/naming.hpp>      // naming::none + name_of (weld_as + name styling)
#include <welder/reflect.hpp>     // welded_for / policy_of / member_bound

/** @file
    The **carriage**: welder's reflection-driven traversal driver, kept separate from
    the `welder::welder<>` entry point (`<welder/welder.hpp>`) it feeds.

    In welding, the *carriage* (or tractor) is the mechanism that drives the torch —
    fed by the **rod** — steadily along the joint. Here it is the entity that walks a
    reflected type or namespace and drives a @ref welder::rod's emission primitives
    along it: it owns *all* the traversal and emission orchestration (base flattening,
    the bindability gate, name resolution, sessions), delegating only the *which
    participates* decisions to a **resolution** policy and the framework-specific
    emission to the rod.

    Two resolutions and their carriage aliases ship: `marker_resolution` /
    @ref welder::stitch_welding_carriage (the default — honor `weld`/`policy`/marks)
    and `greedy_resolution` / @ref welder::tack_welding_carriage (ignore the markers,
    bind an unmarked library greedily). The seam is open: inject a custom
    `basic_carriage<Resolution>` as `welder::welder`'s third template argument.

    Provide the vocabulary first — `#include <welder/vocabulary.hpp>` — then this
    header (`<welder/welder.hpp>`, and each backend header, includes it for you).
*/

namespace welder::inline v0 {

namespace carriages {

// --- resolution policies ----------------------------------------------------
//
// A carriage's *resolution* decides **which** entities participate — the reading of
// welder's markers — kept separate from *how* they are emitted (the carriage body)
// and *whether* they are representable (the bindability gate, which both share). Two
// ship: `marker_resolution` (honor `weld`/`policy`/marks — the default) and
// `greedy_resolution` (ignore the markers, bind everything — tack welding).

/** Stitch-welding resolution: bind only where welder's markers say to.

    A leaf entity participates iff it carries a `weld` for the language and resolves as
    bound under its scope's `policy` and marks; a base is native iff the user welded it;
    a namespace becomes a submodule iff it holds welded content. This is the default —
    an intermittent, marker-directed weld, like a stitch weld. */
struct marker_resolution {
    /** A type/function/variable participates iff welded for @a L. */
    static consteval bool participates(std::meta::info entity, lang L) {
        return welder::welded_for(entity, L);
    }
    /** A base is a separately-registered native base iff the user welded it. */
    static consteval bool is_native_base(std::meta::info base, lang L) {
        return welder::welded_for(base, L);
    }
    /** @a T's native (welded) base list, for the class handle. */
    template <std::meta::info T, lang L>
    static consteval auto native_bases() {
        return detail::native_base_types<T, L>();
    }
    /** A namespace member (class/enum/function/variable) participates. */
    static consteval bool member_participates(std::meta::info mem, lang L,
                                              policy_kind pol) {
        return welder::welded_for(mem, L) && member_bound(mem, L, pol);
    }
    /** A nested namespace participates (recurse + submodule). */
    static consteval bool namespace_participates(std::meta::info ns, lang L,
                                                 policy_kind pol) {
        return member_bound(ns, L, pol) && detail::namespace_has_bound(ns, L);
    }
    /** The gate's registration oracle: welded ⇒ registered (the conservative
        default, = @ref welder::welded_registration). A pure predicate of the
        declaration — never a visited-set — so welding in several passes and
        forward references between welded types stay order-independent. */
    static consteval bool counts_as_registered(std::meta::info type, lang L) {
        return welder::welded_for(type, L);
    }
};

/** Tack-welding resolution: bind an *unmarked* library greedily.

    Named for a **tack weld** — quick spot welds that hold un-prepped work together.
    It ignores welder's `weld` markers entirely: every reflectable type / free function
    / global participates, namespaces recurse greedily, and every public base is
    flattened in (no reliance on a base being separately registered). Bindability is
    *still* enforced — a member whose type is not representable trips the same gate as
    under marker resolution — so a non-representable member reachable from a bound
    entity is a hard error (hatch it with `mark::trust_bindable` / the
    `trust_bindable<T>` variable template, or point the tack at a narrower namespace).
    Any `mark::exclude` that happens to be present is still honored (via
    `member_bound`), so a partially-annotated header can still prune. */
struct greedy_resolution {
    static consteval bool participates(std::meta::info, lang) { return true; }
    static consteval bool is_native_base(std::meta::info, lang) { return false; }
    template <std::meta::info, lang>
    static consteval auto native_bases() {
        return std::array<std::meta::info, 0>{};
    }
    static consteval bool member_participates(std::meta::info mem, lang L,
                                              policy_kind pol) {
        return member_bound(mem, L, pol);
    }
    static consteval bool namespace_participates(std::meta::info ns, lang L,
                                                 policy_kind pol) {
        return member_bound(ns, L, pol) && detail::namespace_has_bindable(ns, L);
    }
    /** The gate's registration oracle: a type the greedy pass itself registers —
        any *complete* class/enum whose marks don't exclude it for @a L — counts,
        so an unmarked library's own types may appear in its signatures without a
        `trust_bindable` hatch.

        Like the stitch oracle this is a pure predicate ("a tack weld of this
        type's namespace registers it"), never a visited-set: tack-welding several
        namespaces in separate passes and forward references within one namespace
        stay order-independent (the frameworks resolve signature types lazily, at
        call time, so registration order does not matter — only that the
        registration exists once the module finishes loading). The trust is
        exactly that: welder cannot know *which* namespaces you tack, so a
        signature naming a registrable type you never weld surfaces as the
        framework's unregistered-type error at call time, not at compile time.

        A **forward-declared** (incomplete) type is deliberately rejected: the
        greedy walk cannot register what has no definition, so a signature naming
        one keeps failing the gate at compile time. */
    static consteval bool counts_as_registered(std::meta::info type, lang L) {
        if (!(std::meta::is_class_type(type) || std::meta::is_enum_type(type)))
            return false;
        if (!std::meta::is_complete_type(type))
            return false;
        return member_bound(type, L, policy_kind::automatic);
    }
};

/** A **carriage**: welder's reflection-driven traversal, parameterized on a
    @a Resolution (which markers it obeys).

    In welding, the *carriage* (or tractor) is the mechanism that drives the torch —
    fed by the **rod** — steadily along the joint. Here it is the entity that walks a
    reflected type or namespace and drives a @ref welder::rod's emission primitives
    along it: it owns *all* the traversal and emission orchestration (base flattening,
    the bindability gate, name resolution, sessions), delegating only the *which
    participates* decisions to @a Resolution and the framework-specific emission to the
    rod. It is a stateless policy struct of static member templates, each parameterized
    on the rod `B` and the name style `Style`.

    Two are aliased for `welder::welder`'s `Carriage` argument (defaulted to
    @ref welder::stitch_welding_carriage): the marker-directed **stitch** carriage and
    the greedy **tack** carriage (@ref welder::tack_welding_carriage). A user may inject
    either, or a bespoke `basic_carriage<CustomResolution>` — the seam is deliberately
    open. The members cross-reference each other (a namespace's class members bind via
    `bind_type`, a nested namespace recurses via `bind_namespace`), so a replacement is
    expected to be a coherent whole rather than a partial override.

    @tparam Resolution the resolution policy (a @ref welder::resolution):
                       `marker_resolution` or `greedy_resolution`, or a bespoke one.
*/
template <resolution Resolution>
struct basic_carriage {
  private:
    /** Flatten the eligible public data members, methods and operators of @a Src onto
        the class handle @a cls (a handle for a type deriving from @a Src).

        A base that @a Resolution treats as native is skipped here (it binds as a base
        of the class handle); the rest are recursed *first* so that, on a name clash,
        the member declared closer to the derived type wins. Constructors are never
        flattened (the derived type provides its own).

        @tparam B     the rod.
        @tparam Src   a reflection of the (base or derived) type whose members to flatten.
        @tparam Style the name style each member's name flows through.
        @tparam Cls   the rod's class-handle type (deduced).
        @param cls the class handle to register onto.
    */
    template <rod B, std::meta::info Src, class Style, class Cls>
    static void bind_members(Cls& cls) {
        constexpr lang L{B::language};
        constexpr auto ctx{std::meta::access_context::unchecked()};

        template for (constexpr auto base :
                      std::define_static_array(welder::public_bases(Src))) {
            if constexpr (!Resolution::is_native_base(base, L))
                bind_members<B, base, Style>(cls);
        }

        constexpr policy_kind pol{policy_of(Src)};

        template for (constexpr auto mem : std::define_static_array(
                          std::meta::nonstatic_data_members_of(Src, ctx))) {
            if constexpr (std::meta::is_public(mem) && member_bound(mem, L, pol)) {
                welder::assert_member_bindable<B, mem, L, Resolution>();
                B::template add_field<mem, Style>(cls);
            }
        }

        template for (constexpr auto fn :
                      std::define_static_array(std::meta::members_of(Src, ctx))) {
            if constexpr (detail::is_bindable_method(fn, L, pol)) {
                welder::assert_callable_bindable<B, fn, L, Resolution>();
                if constexpr (std::meta::is_static_member(fn))
                    B::template add_static_method<fn, Style>(cls);
                else
                    B::template add_method<fn, Style>(cls);
            } else if constexpr (detail::is_operator_candidate(fn, L, pol) &&
                                 B::special_method_name(fn) != nullptr) {
                // A member operator binds like a method, under the backend's special-
                // method name for it (operator+ -> __add__, ...). The specific overload
                // is spliced by add_operator, so unary/binary forms never collide.
                welder::assert_callable_bindable<B, fn, L, Resolution>();
                B::template add_operator<fn>(cls);
            }
        }
    }

  public:
    /** Reflect over enum @a E and register it via rod @a B onto module @a m.

        Emits its docstring, then each enumerator that resolves as bound (an enumerator
        honors the enum's policy and its own exclude/include marks, exactly like a data
        member). `finish_enum` lets the backend apply a whole-enum finalizer — e.g.
        exporting an unscoped enum's values into the enclosing scope, mirroring C++.

        @tparam B the rod.
        @tparam E the enum type.
        @param m    the module handle to register onto.
        @param name the target name (used verbatim, beating any `weld_as`), or
                    `nullptr` to resolve @a E's `weld_as`/styled name.
        @return the rod's enum handle.
    */
    template <rod B, class E, class Style = naming::none>
    static auto bind_enum(typename B::module_type& m, const char* name = nullptr) {
        constexpr lang L{B::language};
        static_assert(Resolution::participates(^^E, L),
                      "welder: weld_type<E>: enum E is not welded for this backend's "
                      "language; annotate it with [[=welder::weld(...)]]");
        const char* enum_name{
            welder::name_of_or<^^E, L, Style, ent_kind::enum_>(name)};
        auto e{B::template make_enum<E>(m, enum_name, welder::doc_of<^^E>())};
        constexpr policy_kind pol{policy_of(^^E)};
        template for (constexpr auto en :
                      std::define_static_array(std::meta::enumerators_of(^^E))) {
            if constexpr (member_bound(en, L, pol))
                B::template add_enumerator<en, Style>(e);
        }
        B::template finish_enum<E>(e);
        return e;
    }

    /** Reflect over @a T and register it via rod @a B onto module @a m.

        Emits:
        - native inheritance from @a T's native ancestors (per @a Resolution; each a
          base of the class handle — bind those bases separately, before @a T);
        - the default constructor (if any), each public non-copy/move constructor, and
          — for a baseless aggregate whose fields all bind — a synthesized field
          constructor;
        - public data members / methods / operators that resolve as bound, plus the
          eligible members of every flattened (non-native) public base.

        @tparam B the rod.
        @tparam T the type to bind.
        @param m    the module handle to register onto.
        @param name the target name (used verbatim, beating any `weld_as`), or
                    `nullptr` to resolve @a T's `weld_as`/styled name.
        @return the rod's class handle, so callers can chain further registrations.
    */
    template <rod B, class T, class Style = naming::none>
    static auto bind_type(typename B::module_type& m, const char* name = nullptr) {
        constexpr lang L{B::language};
        static_assert(Resolution::participates(^^T, L),
                      "welder: weld_type<T>: T is not welded for this backend's "
                      "language; annotate it with [[=welder::weld(...)]]");
        constexpr auto ctx{std::meta::access_context::unchecked()};

        const char* cls_name{
            welder::name_of_or<^^T, L, Style, ent_kind::class_>(name)};

        // Native bases → bases of the class handle; the user binds them first.
        constexpr auto bases{Resolution::template native_bases<^^T, L>()};
        auto cls{B::template make_class<T, bases>(
            m, cls_name, welder::doc_of<^^T>(),
            std::make_index_sequence<bases.size()>{})};

        // Constructors. The type welder actually constructs may be a rod-nominated
        // substitute — a Python trampoline standing in for an abstract base, so a
        // subclass stays constructible even though the base itself is not — exposed
        // as an optional `B::construction_type<T>`; fall back to T when a rod names
        // none (e.g. the Lua rods).
        if constexpr (requires { typename B::template construction_type<T>; }) {
            if constexpr (std::is_default_constructible_v<
                              typename B::template construction_type<T>>)
                B::add_default_ctor(cls);
        } else if constexpr (std::is_default_constructible_v<T>) {
            B::add_default_ctor(cls);
        }
        template for (constexpr auto ctor :
                      std::define_static_array(std::meta::members_of(^^T, ctx))) {
            if constexpr (detail::is_bindable_constructor(ctor)) {
                welder::assert_callable_bindable<B, ctor, L, Resolution>();
                B::template add_constructor<ctor>(cls);
            }
        }
        // An aggregate declares no constructors, so the loop above binds none; give it
        // a synthesized field constructor (brace-init) when eligible.
        if constexpr (detail::aggregate_initializable<T, L>())
            B::template add_aggregate_constructor<T>(cls);

        // Data members + methods + operators (T's own, plus flattened bases).
        bind_members<B, ^^T, Style>(cls);

        return cls;
    }

    /** Register a single free function @a Fn as a module-level function of @a m.

        The semi-manual leaf: gate then emit one function, without walking a
        namespace. @a Fn must reflect a single function (not an overload set) that
        @a Resolution admits for @a B's language; its signature runs the bindability
        gate.

        @tparam B the rod.
        @tparam Fn a reflection of the free function.
        @param m    the module handle to register onto.
        @param name the target name (used verbatim, beating any `weld_as`), or
                    `nullptr` to resolve @a Fn's `weld_as`/styled name.
    */
    template <rod B, std::meta::info Fn, class Style = naming::none>
    static auto bind_function(typename B::module_type& m, const char* name = nullptr) {
        constexpr lang L{B::language};
        static_assert(std::meta::is_function(Fn),
                      "welder: weld_function<Fn>: Fn must reflect a (free) function");
        static_assert(Resolution::participates(Fn, L),
                      "welder: weld_function<Fn>: Fn is not welded for this backend's "
                      "language; annotate it with [[=welder::weld(...)]]");
        welder::assert_callable_bindable<B, Fn, L, Resolution>();
        // Forward the rod's handle (the bound function object, where the
        // framework has one); a void-returning rod makes this void too.
        return B::template add_function<Fn, Style>(m, name);
    }

    /** Register a single namespace variable @a Var as an attribute of @a m.

        The semi-manual leaf for a global/constant: a const/constexpr @a Var becomes a
        value snapshot, a mutable one a live get/set property (backend-dependent). @a Var
        must be admitted by @a Resolution for @a B's language; its type runs the
        bindability gate. The emission runs inside its own per-module session (opened
        and finalized here), so it composes with a later namespace/module weld onto the
        same handle.

        @tparam B the rod.
        @tparam Var a reflection of the namespace-scope variable.
        @param m    the module handle to register onto.
        @param name the target name (used verbatim, beating any `weld_as`), or
                    `nullptr` to resolve @a Var's `weld_as`/styled name.
    */
    template <rod B, std::meta::info Var, class Style = naming::none>
    static auto bind_variable(typename B::module_type& m, const char* name = nullptr) {
        constexpr lang L{B::language};
        static_assert(std::meta::is_variable(Var),
                      "welder: weld_variable<Var>: Var must reflect a namespace "
                      "variable");
        static_assert(Resolution::participates(Var, L),
                      "welder: weld_variable<Var>: Var is not welded for this "
                      "backend's language; annotate it with [[=welder::weld(...)]]");
        welder::assert_member_bindable<B, Var, L, Resolution>();
        auto session{B::open_module(m)};
        // Forward the rod's handle if its add_variable yields one (the shipped
        // rods return void — a bound constant is a snapshot, a mutable global a
        // module property; neither has a framework object worth returning), but
        // the session must still close after the registration.
        if constexpr (std::is_void_v<decltype(B::template add_variable<Var, Style>(
                          m, session, name))>) {
            B::template add_variable<Var, Style>(m, session, name);
            B::close_module(m, session);
        } else {
            auto handle{B::template add_variable<Var, Style>(m, session, name)};
            B::close_module(m, session);
            return handle;
        }
    }

    /** Reflect over a whole namespace @a Ns and expose its members on module @a m.

        @a Resolution decides what participates (marker-directed under
        `marker_resolution`, greedy under `greedy_resolution`); classes bind via
        bind_type(), free functions and namespace variables become module attributes,
        and a nested namespace holding participating content becomes a submodule
        (recursed under its own policy). Members are visited in declaration order.

        @tparam B  the rod.
        @tparam Ns a reflection of the namespace.
        @param m the module handle to fill.
    */
    template <rod B, std::meta::info Ns, class Style = naming::none>
    static void bind_namespace(typename B::module_type& m) {
        static_assert(std::meta::is_namespace(Ns),
                      "welder: weld_namespace<Ns>: Ns must reflect a namespace");
        constexpr lang L{B::language};
        constexpr auto ctx{std::meta::access_context::unchecked()};
        constexpr policy_kind pol{policy_of(Ns)};

        // A [[=welder::doc]] on the namespace becomes the (sub)module docstring.
        if (const char* nsdoc{welder::doc_of<Ns>()})
            B::set_module_doc(m, nsdoc);

        // The backend's per-module session: scratch state for attributes it emits in
        // one batch at the end (e.g. live variable properties). Opened before the
        // walk, finalized by close_module after it.
        auto session{B::open_module(m)};

        template for (constexpr auto mem :
                      std::define_static_array(std::meta::members_of(Ns, ctx))) {
            if constexpr (std::meta::is_type(mem) && std::meta::is_class_type(mem)) {
                if constexpr (Resolution::member_participates(mem, L, pol))
                    bind_type<B, typename [:mem:], Style>(m, nullptr);
            } else if constexpr (std::meta::is_type(mem) && std::meta::is_enum_type(mem)) {
                if constexpr (Resolution::member_participates(mem, L, pol))
                    bind_enum<B, typename [:mem:], Style>(m, nullptr);
            } else if constexpr (std::meta::is_function(mem)) {
                if constexpr (Resolution::member_participates(mem, L, pol)) {
                    welder::assert_callable_bindable<B, mem, L, Resolution>();
                    B::template add_function<mem, Style>(m);
                }
            } else if constexpr (std::meta::is_variable(mem)) {
                if constexpr (Resolution::member_participates(mem, L, pol)) {
                    welder::assert_member_bindable<B, mem, L, Resolution>();
                    B::template add_variable<mem, Style>(m, session);
                }
            } else if constexpr (std::meta::is_namespace(mem)) {
                // A nested namespace resolves like a leaf under the parent policy (but
                // is never welded): it becomes a submodule when it holds participating
                // content (then resolved under its *own* policy).
                if constexpr (Resolution::namespace_participates(mem, L, pol)) {
                    auto sub{B::add_submodule(
                        m, welder::name_of<mem, L, Style, ent_kind::submodule>())};
                    bind_namespace<B, mem, Style>(sub);
                }
            }
        }

        B::close_module(m, session);
    }

    /** Define a submodule of @a m and bind namespace @a Ns into it.

        @tparam B  the rod.
        @tparam Ns a reflection of the namespace.
        @param m    the parent module handle.
        @param name the submodule name (verbatim), or `nullptr` to resolve @a Ns's
                    styled/`weld_as` name.
        @return the new submodule handle.
    */
    template <rod B, std::meta::info Ns, class Style = naming::none>
    static typename B::module_type bind_namespace_as_submodule(
        typename B::module_type& m, const char* name = nullptr) {
        static_assert(std::meta::is_namespace(Ns),
                      "welder: weld_namespace_as_submodule<Ns>: Ns must reflect a "
                      "namespace");
        typename B::module_type sub{B::add_submodule(
            m,
            welder::name_of_or<Ns, B::language, Style, ent_kind::submodule>(name))};
        bind_namespace<B, Ns, Style>(sub);
        return sub;
    }

    /** Fill an existing module out of top-level namespace @a Ns: pre hook, bind the
        namespace, post hook.

        @tparam B     the rod.
        @tparam Ns    a reflection of the (asserted top-level) namespace; its name is
                      meant to be the module name.
        @tparam Style the name style each generated name flows through.
        @tparam Pre   the pre-hook callable type.
        @tparam Post  the post-hook callable type.
        @param m    the module handle to fill.
        @param pre  invoked with @a m before the namespace is bound.
        @param post invoked with @a m after the namespace is bound.
    */
    template <rod B, std::meta::info Ns, class Style = naming::none, class Pre,
              class Post>
    static void build_module(typename B::module_type& m, Pre pre, Post post) {
        static_assert(std::meta::is_namespace(Ns),
                      "welder: weld_module<Ns>: Ns must reflect a namespace");
        static_assert(std::meta::parent_of(Ns) == ^^::,
                      "welder: weld_module<Ns>: Ns must be a top-level namespace (its "
                      "name is meant to be the module name)");
        pre(m);
        bind_namespace<B, Ns, Style>(m);
        post(m);
    }
};

} // namespace carriages

/** The **stitch-welding** carriage (the default): binds only where welder's `weld` /
    `policy` / marks direct — intermittent, marker-driven, like a stitch weld. This is
    `welder::welder`'s default `Carriage`. @see welder::carriages::basic_carriage */
using stitch_welding_carriage = carriages::basic_carriage<carriages::marker_resolution>;

/** The **tack-welding** carriage: binds an *unmarked* library greedily — every
    reflectable type / function / global, namespaces recursed, bases flattened —
    ignoring the `weld` markers, while still enforcing the bindability gate. For
    consuming a third-party library that carries no welder annotations. @see
    welder::carriages::greedy_resolution for the exact rules and caveats. */
using tack_welding_carriage = carriages::basic_carriage<carriages::greedy_resolution>;

/** The default carriage — an alias for @ref welder::stitch_welding_carriage. */
using carriage = stitch_welding_carriage;

} // namespace welder