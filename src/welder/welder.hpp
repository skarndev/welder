#pragma once
#include <array>
#include <concepts>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

#include <welder/bind_traits.hpp> // what-binds selection layer
#include <welder/bindable.hpp>    // bindability gate + caster_oracle
#include <welder/doc.hpp>         // doc_of (class / namespace docstrings)
#include <welder/naming.hpp>      // naming::none + name_of (weld_as + name styling)
#include <welder/reflect.hpp>     // welded_for / policy_of / member_bound

/** @file
    welder's binding entry point: the @ref welder::welder struct, the **rod**
    interface (the @ref welder::rod concept) and the generic binding driver.

    welder's core walks a reflected type/namespace and decides *what* to bind
    (`bind_traits.hpp`) and *whether each type can be represented* (`bindable.hpp`).
    Everything language-specific — *how* to register a class, method, property or
    module attribute — is delegated to a **rod** (a welding rod: the backend that
    lays down the bindings): a stateless policy type (`welder::rods::pybind11::rod<>`,
    `…::nanobind::rod`, `…::sol2::rod`) supplying a fixed set of emission
    primitives. The driver here is written once against the @ref welder::rod concept
    and reused verbatim by every rod, so a new rod implements only its primitives,
    never the traversal/resolution logic.

    The public face of all of it is `welder::welder<Rod>` at the bottom of this
    file: one struct, parameterized on the rod, whose static members run the
    reflection-driven binding at whichever stage of the usual hand-binding flow you
    want to automate (a single type, a namespace into an existing module, a
    namespace as a fresh submodule, or a whole module).

    Provide the vocabulary first — `#include <welder/vocabulary.hpp>` — then this
    header (each backend header includes it for you).
*/

namespace welder {

/** The contract a **rod** (a welder backend, `welder::rods::…::rod`) must satisfy
    to plug into the generic driver.

    A rod `B` is a stateless struct; nothing is inherited, and every member is
    static. The concept statically checks the associated types and the module
    machinery; the class-level and per-member hooks are templated on a
    reflection/type, so they are contract-by-documentation, enforced at the
    driver's instantiation.

    **Associated:**
    @code
    static constexpr lang language;   // the target language B binds to
    using  module_type = ...;         // B's module handle (passed by ref)
    template <class T> static constexpr bool has_native_caster;  // caster_oracle
    @endcode

    **Type binding** (the class handle is whatever `make_class` returns; deduced).
    The per-member hooks take a trailing `class Style` (a
    @ref welder::naming::name_style) so the rod resolves its own name via
    `welder::name_of<Mem, language, Style, ent_kind::…>()` — which also applies any
    `[[=welder::weld_as]]` override. The class name is styled by the driver and
    passed to `make_class` ready-made:
    @code
    template <class T, auto Bases, std::size_t... I>
      static auto make_class(module_type&, const char* name, const char* doc,
                             std::index_sequence<I...>);   // Bases[I] spliced
    static void add_default_ctor(auto& cls);
    template <std::meta::info Ctor> static void add_constructor(auto& cls);
    template <class T>              static void add_aggregate_constructor(auto& cls);
    template <std::meta::info Mem, class Style> static void add_field(auto& cls);
    template <std::meta::info Fn,  class Style> static void add_method(auto& cls);
    template <std::meta::info Fn,  class Style> static void add_static_method(auto& cls);
    template <std::meta::info Fn>   static void add_operator(auto& cls); // fixed op name
    static consteval const char* special_method_name(std::meta::info op_fn);
        // target special-method name for a member operator, or nullptr if the
        // backend does not expose it (drives add_operator eligibility)
    @endcode

    **Enum binding** (the enum handle is whatever `make_enum` returns; deduced):
    @code
    template <class E> static auto make_enum(module_type&, const char* name,
                                             const char* doc);
    template <std::meta::info Enum, class Style> static void add_enumerator(auto& e);
    template <class E> static void finish_enum(auto& e); // e.g. export unscoped
    @endcode

    **Namespace / module binding** (a "session" is backend scratch state — e.g. an
    accumulator for deferred, batched attributes — obtained per (sub)module):
    @code
    static auto open_module(module_type&);              // -> session
    static void set_module_doc(module_type&, const char* doc);
    template <std::meta::info Fn,  class Style> static void add_function(module_type&, const char* name = nullptr);
    template <std::meta::info Var, class Style> static void add_variable(module_type&, session&, const char* name = nullptr);
    static module_type add_submodule(module_type&, const char* name); // name pre-styled
    static void close_module(module_type&, session&);   // finalize the session
    @endcode

    @tparam B the candidate rod type.
*/
template <class B>
concept rod =
    caster_oracle<B> &&
    requires {
        { B::language } -> std::convertible_to<lang>;
        typename B::module_type;
    } &&
    requires(typename B::module_type& m, const char* s,
             std::remove_cvref_t<decltype(B::open_module(
                 std::declval<typename B::module_type&>()))> session) {
        { B::open_module(m) };
        { B::add_submodule(m, s) } -> std::same_as<typename B::module_type>;
        B::set_module_doc(m, s);
        B::close_module(m, session);
    };

namespace detail {

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
        return native_base_types<T, L>();
    }
    /** A namespace member (class/enum/function/variable) participates. */
    static consteval bool member_participates(std::meta::info mem, lang L,
                                              policy_kind pol) {
        return welder::welded_for(mem, L) && member_bound(mem, L, pol);
    }
    /** A nested namespace participates (recurse + submodule). */
    static consteval bool namespace_participates(std::meta::info ns, lang L,
                                                 policy_kind pol) {
        return member_bound(ns, L, pol) && namespace_has_bound(ns, L);
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
        return member_bound(ns, L, pol) && namespace_has_bindable(ns, L);
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

    @tparam Resolution the resolution policy: `marker_resolution` or `greedy_resolution`.
*/
template <class Resolution>
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
                welder::assert_member_bindable<B, mem, L>();
                B::template add_field<mem, Style>(cls);
            }
        }

        template for (constexpr auto fn :
                      std::define_static_array(std::meta::members_of(Src, ctx))) {
            if constexpr (is_bindable_method(fn, L, pol)) {
                welder::assert_callable_bindable<B, fn, L>();
                if constexpr (std::meta::is_static_member(fn))
                    B::template add_static_method<fn, Style>(cls);
                else
                    B::template add_method<fn, Style>(cls);
            } else if constexpr (is_operator_candidate(fn, L, pol) &&
                                 B::special_method_name(fn) != nullptr) {
                // A member operator binds like a method, under the backend's special-
                // method name for it (operator+ -> __add__, ...). The specific overload
                // is spliced by add_operator, so unary/binary forms never collide.
                welder::assert_callable_bindable<B, fn, L>();
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
            name ? name : welder::name_of<^^E, L, Style, ent_kind::enum_>()};
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
            name ? name : welder::name_of<^^T, L, Style, ent_kind::class_>()};

        // Native bases → bases of the class handle; the user binds them first.
        constexpr auto bases{Resolution::template native_bases<^^T, L>()};
        auto cls{B::template make_class<T, bases>(
            m, cls_name, welder::doc_of<^^T>(),
            std::make_index_sequence<bases.size()>{})};

        // Constructors.
        if constexpr (std::is_default_constructible_v<T>)
            B::add_default_ctor(cls);
        template for (constexpr auto ctor :
                      std::define_static_array(std::meta::members_of(^^T, ctx))) {
            if constexpr (is_bindable_constructor(ctor)) {
                welder::assert_callable_bindable<B, ctor, L>();
                B::template add_constructor<ctor>(cls);
            }
        }
        // An aggregate declares no constructors, so the loop above binds none; give it
        // a synthesized field constructor (brace-init) when eligible.
        if constexpr (aggregate_initializable<T, L>())
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
    static void bind_function(typename B::module_type& m, const char* name = nullptr) {
        constexpr lang L{B::language};
        static_assert(std::meta::is_function(Fn),
                      "welder: weld_function<Fn>: Fn must reflect a (free) function");
        static_assert(Resolution::participates(Fn, L),
                      "welder: weld_function<Fn>: Fn is not welded for this backend's "
                      "language; annotate it with [[=welder::weld(...)]]");
        welder::assert_callable_bindable<B, Fn, L>();
        B::template add_function<Fn, Style>(m, name);
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
    static void bind_variable(typename B::module_type& m, const char* name = nullptr) {
        constexpr lang L{B::language};
        static_assert(std::meta::is_variable(Var),
                      "welder: weld_variable<Var>: Var must reflect a namespace "
                      "variable");
        static_assert(Resolution::participates(Var, L),
                      "welder: weld_variable<Var>: Var is not welded for this "
                      "backend's language; annotate it with [[=welder::weld(...)]]");
        welder::assert_member_bindable<B, Var, L>();
        auto session{B::open_module(m)};
        B::template add_variable<Var, Style>(m, session, name);
        B::close_module(m, session);
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
                    welder::assert_callable_bindable<B, mem, L>();
                    B::template add_function<mem, Style>(m);
                }
            } else if constexpr (std::meta::is_variable(mem)) {
                if constexpr (Resolution::member_participates(mem, L, pol)) {
                    welder::assert_member_bindable<B, mem, L>();
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
            m, name ? name
                    : welder::name_of<Ns, B::language, Style, ent_kind::submodule>())};
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

} // namespace detail

/** The **stitch-welding** carriage (the default): binds only where welder's `weld` /
    `policy` / marks direct — intermittent, marker-driven, like a stitch weld. This is
    `welder::welder`'s default `Carriage`. @see welder::detail::basic_carriage */
using stitch_welding_carriage = detail::basic_carriage<detail::marker_resolution>;

/** The **tack-welding** carriage: binds an *unmarked* library greedily — every
    reflectable type / function / global, namespaces recursed, bases flattened —
    ignoring the `weld` markers, while still enforcing the bindability gate. For
    consuming a third-party library that carries no welder annotations. @see
    welder::detail::greedy_resolution for the exact rules and caveats. */
using tack_welding_carriage = detail::basic_carriage<detail::greedy_resolution>;

/** The default carriage — an alias for @ref welder::stitch_welding_carriage. */
using carriage = stitch_welding_carriage;

/** welder's binding entry point, parameterized on a **rod**.

    One struct is all a user drives: pick a rod (e.g. `welder::rods::pybind11::rod<>`,
    from that rod's header) and call the static member matching the stage of the
    usual hand-binding flow you want to automate — welder generates the
    backend-agnostic boilerplate there, and everything around it stays ordinary
    hand-written binding code.

    @code
    #include <welder/rods/python/pybind11/rod.hpp>
    using weld = welder::welder<welder::rods::pybind11::rod<>>;

    PYBIND11_MODULE(mymod, m) {
        weld::weld_type<MyType>(m);          // one type onto an existing module
        weld::weld_function<^^my_free_fn>(m);// one free function onto m
        weld::weld_variable<^^my_global>(m); // one global/constant onto m
        weld::weld_namespace<^^myns>(m);     // a namespace's members into m
        weld::weld_namespace_as_submodule<^^other>(m); // …or into m.other
    }
    @endcode

    (For zero hand-written entry-point code at all, each rod also ships a
    `module.hpp` with the `WELDER_MODULE` entry-point macro.)

    **Composition / subclassing.** Every entry point is a one-line forward to the
    injected @ref welder::carriage "Carriage" (the traversal driver), which owns the
    resolution and the welded/bindability gates. There are two ways to go beyond the
    stock flow, both without re-deriving any of that: **inject** a different carriage
    as the third template argument (e.g. @ref welder::tack_welding_carriage to bind an
    unmarked library), or **subclass** `welder::welder` — the whole class is static and
    non-virtual, so deriving simply gives a user's own driver type access to the bound
    Rod / Style / Carriage (via `rod_type` / `name_style` / `carriage_type`) and to the
    public entry points, to assemble bespoke routines (a hand-picked subset of a
    namespace, welded and hand-written registrations interleaved) from the carriage's
    gated building blocks.

    @tparam B     the rod to emit through (any type satisfying @ref welder::rod).
    @tparam Style the name style every generated name flows through — a class,
                  method, field, … is renamed into the target language's convention
                  (see `<welder/naming.hpp>`). A `[[=welder::weld_as]]` override on an
                  entity beats the style and is used verbatim (and a call-site `name`
                  argument beats even that). Defaults to @ref welder::naming::none (bind
                  C++ identifiers unchanged); a Python binding might pass
                  `welder::rods::python::pep8`.
    @tparam Carriage the reflection-driven traversal driver (@ref welder::carriage) —
                  what walks a type/namespace and orchestrates the rod's emission.
                  Defaults to @ref welder::stitch_welding_carriage (marker-directed);
                  inject @ref welder::tack_welding_carriage to bind an unmarked library
                  greedily, or any `basic_carriage<Resolution>`.
*/
template <rod B, class Style = naming::none, class Carriage = carriage>
struct welder {
    /** The rod this instantiation emits through. */
    using rod_type = B;
    /** The name style this instantiation renames through. */
    using name_style = Style;
    /** The traversal driver (carriage) this instantiation drives through. */
    using carriage_type = Carriage;
    /** The rod's module handle (`py::module_`, `sol::table`, …). */
    using module_type = typename B::module_type;

    /** A do-nothing module hook; the default for weld_module()'s pre/post. */
    static constexpr auto noop{[](module_type&) {}};

    /** Reflect over @a T and register it on module @a m.

        A class type binds with its constructors, fields, methods, operators and
        native bases; an enum with its enumerators (see the carriage for the full set).
        Under the default (stitch) carriage @a T must be welded for `B::language`.

        @tparam T the class or enum type to bind.
        @param m    the module handle to register onto.
        @param name the target-language name (verbatim, beats `weld_as`), or `nullptr`
                    to resolve @a T's `weld_as`/styled name.
        @return the rod's class/enum handle, for chaining further
                registrations. */
    template <class T>
    static auto weld_type(module_type& m, const char* name = nullptr) {
        if constexpr (std::is_enum_v<T>)
            return Carriage::template bind_enum<B, T, Style>(m, name);
        else
            return Carriage::template bind_type<B, T, Style>(m, name);
    }

    /** Reflect over free function @a Fn and register it on module @a m.

        The semi-manual analogue of what namespace binding does for a namespace's
        free functions: bind one hand-picked function directly, without welding the
        whole enclosing namespace. @a Fn must reflect a single function (an overload
        set is ambiguous — reflect the specific overload you want); under the default
        (stitch) carriage it must be welded for `B::language`.

        @tparam Fn a reflection of the free function.
        @param m    the module handle to register onto.
        @param name the target name, used **verbatim** and taking precedence over any
                    `[[=welder::weld_as]]` on @a Fn; `nullptr` (default) resolves the
                    `weld_as`/styled name. */
    template <std::meta::info Fn>
    static void weld_function(module_type& m, const char* name = nullptr) {
        Carriage::template bind_function<B, Fn, Style>(m, name);
    }

    /** Reflect over global/namespace variable @a Var and register it on module @a m.

        The semi-manual analogue of what namespace binding does for a namespace's
        variables: expose one hand-picked global or constant directly. A
        const/constexpr @a Var becomes a value snapshot; a mutable one a live get/set
        property over the C++ global. Under the default (stitch) carriage @a Var must
        be welded for `B::language`.

        @tparam Var a reflection of the namespace-scope variable.
        @param m    the module handle to register onto.
        @param name the target name, used **verbatim** and taking precedence over any
                    `[[=welder::weld_as]]` on @a Var; `nullptr` (default) resolves the
                    `weld_as`/styled name. */
    template <std::meta::info Var>
    static void weld_variable(module_type& m, const char* name = nullptr) {
        Carriage::template bind_variable<B, Var, Style>(m, name);
    }

    /** Reflect over namespace @a Ns and expose its members on module @a m.

        Classes and enums bind via weld_type(), free functions and namespace
        variables become module attributes, and a nested namespace holding
        participating content becomes a submodule. Which members participate is the
        carriage's call (marker-directed by default, greedy under the tack carriage).

        @tparam Ns a reflection of the namespace.
        @param m the module handle to fill.
        @return @a m, for chaining. */
    template <std::meta::info Ns>
    static module_type& weld_namespace(module_type& m) {
        Carriage::template bind_namespace<B, Ns, Style>(m);
        return m;
    }

    /** Define a submodule of @a m (via the backend) and weld_namespace() @a Ns
        into it.

        @tparam Ns a reflection of the namespace.
        @param m    the parent module handle.
        @param name the submodule name (verbatim), or `nullptr` to resolve @a Ns's
                    styled/`weld_as` name.
        @return the new submodule handle, for chaining. */
    template <std::meta::info Ns>
    static module_type weld_namespace_as_submodule(module_type& m,
                                                   const char* name = nullptr) {
        return Carriage::template bind_namespace_as_submodule<B, Ns, Style>(m, name);
    }

    /** Build a whole module out of top-level namespace @a Ns: run @a pre, weld the
        namespace into @a m (adopting a namespace-level `doc` as the module
        docstring), run @a post.

        The hooks fold hand-written bindings in around welder's generated body.
        This fills an *existing* module handle; pair it with an entry-point macro
        (the framework's own, or the backend's `WELDER_MODULE` expansion, which
        calls this).

        @tparam Ns   a reflection of the top-level namespace (its name is meant to
                     be the module name).
        @tparam Pre  the pre-hook callable type.
        @tparam Post the post-hook callable type.
        @param m    the module handle to fill.
        @param pre  invoked with @a m before binding (defaults to noop).
        @param post invoked with @a m after binding (defaults to noop).
        @return @a m, for chaining. */
    template <std::meta::info Ns, class Pre = decltype(noop),
              class Post = decltype(noop)>
    static module_type& weld_module(module_type& m, Pre pre = noop,
                                    Post post = noop) {
        Carriage::template build_module<B, Ns, Style>(m, pre, post);
        return m;
    }
};

} // namespace welder
