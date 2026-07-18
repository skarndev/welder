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

namespace detail {

/** Is @a Alias the *only* participating alias in @a Ns welding its specialization?

    Two aliases naming the same instantiation would register it twice (a framework
    error at import time); the carriage diagnoses it at compile time instead. A
    non-participating duplicate (excluded, or not welded under this resolution) is
    fine — it simply doesn't bind.
    @tparam Resolution the carriage's resolution policy.
    @tparam Ns    the namespace being swept.
    @tparam Alias the alias under consideration.
    @param L   the target language.
    @param pol the namespace's policy.
    @return `true` iff no *other* participating alias dealiases to the same type. */
template <class Resolution, std::meta::info Ns, std::meta::info Alias>
consteval bool sole_alias_of_target(lang L, policy_kind pol) {
    for (auto m :
         std::meta::members_of(Ns, std::meta::access_context::unchecked())) {
        if (!welder::names_template_specialization(m))
            continue;
        // Self-identity by NAME, not by `==`: gcc-16 compares two alias
        // reflections of the same underlying type as equal, which would make the
        // duplicate invisible. Two aliases cannot share an identifier in one
        // namespace, so the identifier is the reliable identity here.
        if (std::meta::identifier_of(m) == std::meta::identifier_of(Alias))
            continue;
        if (std::meta::dealias(m) == std::meta::dealias(Alias) &&
            Resolution::alias_participates(m, L, pol, Ns))
            return false;
    }
    return true;
}

/** The bound name of the type welded through @a Alias (a namespace-scope alias
    to a specialization, or a member alias inside a welded class).

    Resolution order: a `weld_as` on the alias (most specific, verbatim) → a
    `weld_as` on the target type (read through an instantiation from its
    template; note a template-level one names every instantiation alike, so
    pairing it with several aliases of one template collides) → the alias's
    identifier, reshaped by @a Style.  This is @ref welder::name_of applied to
    the alias, with the target-level `weld_as` fallback spliced in between its
    two steps.
    @tparam Alias the alias through which the type is welded.
    @tparam L     the target language.
    @tparam Style the name style.
    @tparam Kind  the entity kind the style hook receives (class_ or enum_).
    @return the bound name, in static storage. */
template <std::meta::info Alias, lang L, class Style,
          ent_kind Kind = ent_kind::class_>
consteval const char* alias_bound_name() {
    if constexpr (welder::weld_as_of<Alias, L>() != nullptr)
        return welder::weld_as_of<Alias, L>();
    else if constexpr (welder::weld_as_of<std::meta::dealias(Alias), L>() !=
                       nullptr)
        return welder::weld_as_of<std::meta::dealias(Alias), L>();
    else
        return welder::name_of<Alias, L, Style, Kind>();
}

} // namespace detail

/** Is @a type declared at class scope — a *nested* (member) type?

    @param type a reflection of a class/enum type.
    @return `true` iff the immediately enclosing scope is a class. */
consteval bool is_nested_type(std::meta::info type) {
    const std::meta::info outer{std::meta::parent_of(type)};
    return std::meta::is_type(outer) && std::meta::is_class_type(outer);
}

namespace detail {

/** The GATE side of the nested-type sweep: does class-scoped @a type register
    with its enclosing class's binding under @a Resolution?

    Mirrors `basic_carriage::bind_nested_types`' selection exactly — a nested
    type resolves like any other class member (the *outer's* policy plus the
    type's own exclude/include/only marks, with the usual access admission),
    never via its own `weld` — so the bindability gate promises a registration
    precisely when the sweep provides one. The enclosing class must itself count
    as registered (recursively, for deeper nesting). Unnameable (unnamed) and
    incomplete member types never register, and a member type *alias* is not a
    declaration the sweep visits — callers see `false` through the
    identifier/completeness checks and the alias never reaching here dealiased
    to a namespace-scope parent.

    @tparam Resolution the resolution whose sweep is being promised for.
    @param type a class-scoped class/enum type (see @ref welder::is_nested_type).
    @param L    the target language.
    @return `true` iff welding the enclosing class registers @a type. */
template <class Resolution>
consteval bool nested_type_registered(std::meta::info type, lang L) {
    const std::meta::info outer{std::meta::parent_of(type)};
    return std::meta::has_identifier(type) && std::meta::is_complete_type(type) &&
           detail::member_access_admitted<Resolution>(type, L, outer) &&
           member_bound(type, L, policy_of(outer)) &&
           Resolution::counts_as_registered(outer, L);
}

/** Is @a Alias the *only* member alias of @a Outer whose participation would
    register its target?

    Two participating member aliases naming the same target would register it
    twice (a framework load error), diagnosed at compile time — the class-scope
    twin of @ref sole_alias_of_target. The gate side of participation needs no
    re-check here: siblings share the target, so when the caller (an alias that
    IS registering) consults this, a marks/access-participating sibling
    registers too.
    @tparam Resolution the carriage's resolution policy.
    @tparam Outer the class being bound.
    @tparam Alias the member alias under consideration.
    @param L   the target language.
    @param pol the outer's policy.
    @return `true` iff no *other* participating member alias shares the target. */
template <class Resolution, std::meta::info Outer, std::meta::info Alias>
consteval bool sole_member_alias_of_target(lang L, policy_kind pol) {
    for (auto m : std::meta::members_of(Outer,
                                        std::meta::access_context::unchecked())) {
        if (!std::meta::is_type_alias(m) || !std::meta::has_identifier(m))
            continue;
        // Self-identity by NAME (== collapses alias reflections on gcc-16; two
        // member aliases cannot share an identifier in one class).
        if (std::meta::identifier_of(m) == std::meta::identifier_of(Alias))
            continue;
        if (std::meta::dealias(m) == std::meta::dealias(Alias) &&
            member_access_admitted<Resolution>(m, L, Outer) &&
            Resolution::class_member_participates(m, L, pol, Outer))
            return false;
    }
    return true;
}

/** Does a participating **member alias** of @a scope name @a type — i.e. would
    the sweep of @a scope register @a type under that alias?

    The alias side of the scope-aware oracle (see @ref scoped_registration).
    Deliberately no bindability re-check on the target: the oracle is consulted
    only where every other `bindable()` branch has already failed, which is
    exactly the sweep's register-vs-skip arbiter — so "an admitted, participating
    alias names it" is the whole remaining question.
    @tparam Resolution the carriage's resolution policy.
    @param scope the class whose member aliases to scan.
    @param type  the candidate target type.
    @param L     the target language.
    @return `true` iff a participating member alias of @a scope names @a type. */
template <class Resolution>
consteval bool registered_by_member_alias(std::meta::info scope,
                                          std::meta::info type, lang L) {
    const policy_kind pol{policy_of(scope)};
    for (auto m : std::meta::members_of(scope,
                                        std::meta::access_context::unchecked())) {
        if (!std::meta::is_type_alias(m) || !std::meta::has_identifier(m))
            continue;
        if (std::meta::dealias(m) != type)
            continue;
        if (member_access_admitted<Resolution>(m, L, scope) &&
            Resolution::class_member_participates(m, L, pol, scope))
            return true;
    }
    return false;
}

/** The **scope-aware registration oracle**: @a Resolution widened with the
    member-alias registrations of one class.

    A member alias's `weld`-free participation is invisible from the target type
    (an alias is unrecoverable from the type it names), so the plain oracle
    cannot vouch for the types a class's own aliases register. The carriage
    therefore gates the class's members through this wrapper — @a Scope is the
    welded type being bound — which additionally counts (a) types a
    participating member alias of @a Scope registers, and (b) the nested-type
    chain re-run alias-aware, so an alias target's own nested types recurse
    through here too. Cross-class use of an alias-registered type stays
    `trust_bindable` territory, consistent with the namespace-alias blind spot.

    Inherits @a Resolution so every participation hook (including an optional
    `protected_participates`) keeps working when this wrapper is threaded where
    a resolution is expected; only the oracle is shadowed.
    @tparam Resolution the carriage's resolution policy.
    @tparam Scope      the welded class whose members are being gated. */
template <class Resolution, std::meta::info Scope>
struct scoped_registration : Resolution {
    static consteval bool counts_as_registered(std::meta::info type, lang L) {
        if (Resolution::counts_as_registered(type, L))
            return true;
        if (!(std::meta::is_class_type(type) || std::meta::is_enum_type(type)))
            return false;
        // The nested chain, alias-aware: a nested type of an alias-registered
        // target resolves through this oracle again.
        if (welder::is_nested_type(type) &&
            nested_type_registered<scoped_registration>(type, L))
            return true;
        return registered_by_member_alias<Resolution>(Scope, type, L);
    }
};

} // namespace detail

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
    /** A base is a separately-registered native base iff the user welded it.
        The trailing reflection is the type whose direct base list is being
        walked (context for bespoke rules; unused here). */
    static consteval bool is_native_base(std::meta::info base, lang L,
                                         std::meta::info /*bound_into*/) {
        return welder::welded_for(base, L);
    }
    /** @a T's native (welded) base list, for the class handle. */
    template <std::meta::info T, lang L>
    static consteval auto native_bases() {
        return detail::native_base_types<T, L>();
    }
    /** A namespace member (class/enum/function/variable) participates. The
        trailing reflection is the namespace being swept (== `parent_of(mem)`;
        context for bespoke rules; unused here). */
    static consteval bool member_participates(std::meta::info mem, lang L,
                                              policy_kind pol,
                                              std::meta::info /*bound_into*/) {
        return welder::welded_for(mem, L) && member_bound(mem, L, pol);
    }
    /** A namespace-scope alias to a class-template specialization participates:
        welded via the alias's own `weld` (precedence) or the template's (read
        through the instantiation), with the usual scope-policy/marks resolution
        against the instantiation. @see welder::alias_welded_for */
    static consteval bool alias_participates(std::meta::info mem, lang L,
                                             policy_kind pol,
                                             std::meta::info /*bound_into*/) {
        return welder::alias_welded_for(mem, L) &&
               member_bound(std::meta::dealias(mem), L, pol);
    }
    /** A *class* member (field / method / operator / constructor — and, loosely,
        an enumerator) participates: its scope's policy + its own marks decide.
        Resolved per overload, so a mark on one overload (or one constructor)
        excludes just that one. The trailing reflection is the entity whose
        binding receives the member — the welded type, held fixed through the
        base-flattening recursion (== `parent_of(mem)` except for a flattened
        base's member); context for bespoke rules, unused here. */
    static consteval bool class_member_participates(std::meta::info mem, lang L,
                                                    policy_kind pol,
                                                    std::meta::info /*bound_into*/) {
        return member_bound(mem, L, pol);
    }
    /** A **protected** member is admitted iff its declaring class says so — a
        `policy::weld_protected` annotation covering @a L (also the carriage's
        default when a resolution declares no hook; spelled out here as the
        documented contract). Public members are always admitted and private
        members never are — both decided *before* this hook (see
        `detail::member_access_admitted`), so it arbitrates protected only.
        The trailing reflection is the entity whose binding receives the
        member — a bespoke hook can key on it ("admit this mixin's protected
        members, but only into Derived"); the shipped rule reads the declaring
        class. */
    static consteval bool protected_participates(std::meta::info mem, lang L,
                                                 std::meta::info /*bound_into*/) {
        return welder::protected_welded(std::meta::parent_of(mem), L);
    }
    /** A nested namespace participates (recurse + submodule). The trailing
        reflection is the parent namespace (== `parent_of(ns)`; unused here). */
    static consteval bool namespace_participates(std::meta::info ns, lang L,
                                                 policy_kind pol,
                                                 std::meta::info /*bound_into*/) {
        return member_bound(ns, L, pol) && detail::namespace_has_bound(ns, L);
    }
    /** The gate's registration oracle: welded ⇒ registered (the
        @ref welder::welded_registration rule), plus the **nested-type** rule —
        a class-scoped type registers with its enclosing class's binding, so it
        counts iff it resolves as a member of a counting outer (see
        `detail::nested_type_registered`, the exact mirror of the carriage's
        nested-type sweep). A pure predicate of the declaration — never a
        visited-set — so welding in several passes and forward references
        between welded types stay order-independent. */
    static consteval bool counts_as_registered(std::meta::info type, lang L) {
        if (welder::welded_for(type, L))
            return true;
        if ((std::meta::is_class_type(type) || std::meta::is_enum_type(type)) &&
            welder::is_nested_type(type))
            return detail::nested_type_registered<marker_resolution>(type, L);
        return false;
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
    `member_bound`), so a partially-annotated header can still prune.

    @tparam WeldProtected admit every type's **protected** members (default
            `false`: public only, like the stitch default). The knob exists
            because a third-party library cannot carry a
            `[[=welder::policy::weld_protected]]` annotation — this is the tack
            analogue, blanket for the whole pass:
            `welder::carriages::basic_carriage<welder::carriages::greedy_resolution<true>>`.
            An annotation that *is* present (a partially-annotated header) is
            honored either way; for surgical per-member control, override
            `protected_participates` in a bespoke resolution. Private members
            stay out regardless — that boundary is not a knob. */
template <bool WeldProtected = false>
struct greedy_resolution {
    static consteval bool participates(std::meta::info, lang) { return true; }
    static consteval bool is_native_base(std::meta::info, lang,
                                         std::meta::info /*bound_into*/) {
        return false;
    }
    template <std::meta::info, lang>
    static consteval auto native_bases() {
        return std::array<std::meta::info, 0>{};
    }
    static consteval bool member_participates(std::meta::info mem, lang L,
                                              policy_kind pol,
                                              std::meta::info /*bound_into*/) {
        return member_bound(mem, L, pol);
    }
    /** Greedy: an alias-declared specialization binds like any other type — no
        `weld` needed on alias or template; marks (via the instantiation) still prune. */
    static consteval bool alias_participates(std::meta::info mem, lang L,
                                             policy_kind pol,
                                             std::meta::info /*bound_into*/) {
        return member_bound(std::meta::dealias(mem), L, pol);
    }
    /** Same as stitch: greedy ignores the `weld` marker, not the marks — a mark on
        an individual overload/constructor still prunes it. */
    static consteval bool class_member_participates(std::meta::info mem, lang L,
                                                    policy_kind pol,
                                                    std::meta::info /*bound_into*/) {
        return member_bound(mem, L, pol);
    }
    /** Protected members: the @a WeldProtected knob (the whole-pass blanket for
        an unannotatable third-party library), or — knob off — the annotation,
        exactly like stitch. Arbitrates protected only: public/private are
        decided before the hook (see `detail::member_access_admitted`, which
        also documents the trailing bound-into reflection). */
    static consteval bool protected_participates(std::meta::info mem, lang L,
                                                 std::meta::info /*bound_into*/) {
        return WeldProtected ||
               welder::protected_welded(std::meta::parent_of(mem), L);
    }
    static consteval bool namespace_participates(std::meta::info ns, lang L,
                                                 policy_kind pol,
                                                 std::meta::info /*bound_into*/) {
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
        one keeps failing the gate at compile time.

        A **class-scoped (nested)** type registers with its enclosing class's
        binding — the greedy pass sweeps member types exactly like the stitch one
        (the outer's policy + the type's own marks, access admitted), so it
        counts via `detail::nested_type_registered`, recursing into the enclosing
        class. */
    static consteval bool counts_as_registered(std::meta::info type, lang L) {
        if (!(std::meta::is_class_type(type) || std::meta::is_enum_type(type)))
            return false;
        if (!std::meta::is_complete_type(type))
            return false;
        if (welder::is_nested_type(type))
            return detail::nested_type_registered<greedy_resolution>(type, L);
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
    /** Flatten the eligible data members, methods and operators of @a Src onto
        the class handle @a cls (a handle for a type deriving from @a Src) —
        public ones, plus protected ones whose access is admitted (see
        `detail::member_access_admitted`; private never binds).

        A base that @a Resolution treats as native is skipped here (it binds as a base
        of the class handle); the rest are recursed *first* so that, on a name clash,
        the member declared closer to the derived type wins. Constructors are never
        flattened (the derived type provides its own).

        @tparam B         the rod.
        @tparam BoundInto the welded type whose class handle receives the members —
                          held fixed through the flattening recursion, and handed to
                          the resolution hooks as their bound-into context.
        @tparam Src   a reflection of the (base or derived) type whose members to flatten.
        @tparam Style the name style each member's name flows through.
        @tparam Cls   the rod's class-handle type (deduced).
        @param cls the class handle to register onto.
    */
    template <rod B, std::meta::info BoundInto, std::meta::info Src, class Style,
              class Cls>
    static void bind_members(Cls& cls) {
        constexpr lang L{B::language};
        constexpr auto ctx{std::meta::access_context::unchecked()};

        template for (constexpr auto base :
                      std::define_static_array(welder::public_bases(Src))) {
            if constexpr (!Resolution::is_native_base(base, L, Src))
                bind_members<B, BoundInto, base, Style>(cls);
        }

        constexpr policy_kind pol{policy_of(Src)};

        // The gate for class members runs through the SCOPE-AWARE oracle: the
        // bound-into type's own member aliases register types the plain oracle
        // cannot see (an alias is unrecoverable from the type it names).
        using Reg = detail::scoped_registration<Resolution, BoundInto>;

        template for (constexpr auto mem : std::define_static_array(
                          std::meta::nonstatic_data_members_of(Src, ctx))) {
            // UNNAMED data members — anonymous unions, unnamed bit-fields —
            // are structurally unbindable (there is nothing to name the
            // attribute by; an anonymous union also has no declarator to
            // carry a mark, and unions never bind anyway), so they are
            // skipped before resolution, like unnamed nested types. The
            // enclosing type's named members still bind.
            if constexpr (std::meta::has_identifier(mem) &&
                          detail::member_access_admitted<Resolution>(mem, L,
                                                                     BoundInto) &&
                          Resolution::class_member_participates(mem, L, pol,
                                                                BoundInto)) {
                welder::assert_member_bindable<B, mem, L, Reg>();
                B::template add_field<mem, Style>(cls);
            }
        }

        // Methods are emitted as whole OVERLOAD GROUPS: the walk fires on each
        // group's first participating overload (the leader), gathers the
        // resolution-admitted set, gates every member, and hands the group to
        // the rod in one call — so one-value-per-name frameworks (the Lua rods)
        // register a complete set, and per-overload marks / bespoke resolutions
        // shape the group identically on every rod. (Operators are NOT handled
        // here: their slot groups span this flattening recursion AND the
        // anchored free operators of the enclosing namespace, so they are
        // emitted once per welded type — see bind_operators.)
        template for (constexpr auto fn :
                      std::define_static_array(std::meta::members_of(Src, ctx))) {
            // An accessor-marked function (getter/setter covering L) is not a
            // method here — it binds as a PROPERTY (see bind_properties); for
            // languages its marks don't cover, it stays an ordinary method.
            if constexpr (detail::is_method_candidate(fn) &&
                          !welder::is_accessor_for(fn, L) &&
                          detail::member_access_admitted<Resolution>(fn, L,
                                                                     BoundInto) &&
                          Resolution::class_member_participates(fn, L, pol,
                                                                BoundInto)) {
                if constexpr (detail::is_overload_leader<
                                  detail::method_overload_set<Resolution>>(
                                  fn, L, BoundInto)) {
                    constexpr auto grp{detail::overload_group<
                        detail::method_overload_set<Resolution>, fn, L,
                        BoundInto>()};
                    template for (constexpr auto member :
                                  std::define_static_array(grp)) {
                        welder::assert_callable_bindable<B, member, L, Reg>();
                    }
                    if constexpr (std::meta::is_static_member(fn))
                        B::template add_static_method<grp, Style>(cls);
                    else
                        B::template add_method<grp, Style>(cls);
                }
            }
            // A member function TEMPLATE falls through every branch: not a
            // bindable entity (welder cannot invent its template arguments) —
            // bind an instantiation by chaining it onto the class handle
            // weld_type returns. A participation mark on one is inert, and NOT
            // diagnosable: P2996 refuses annotations_of on an uninstantiated
            // template (the mark is readable only through an instantiation), so
            // welder cannot even see it here to fail fast.
        }
    }

    /** The per-enumerator walk shared by `bind_enum` and `bind_nested_enum`:
        each enumerator resolves like a data member (the enum's policy + its own
        marks) and is emitted through the rod's `add_enumerator`.
        @tparam B     the rod.
        @tparam E     the enum type.
        @tparam Style the name style.
        @param e the rod's enum handle. */
    template <rod B, class E, class Style, class EnumHandle>
    static void emit_enumerators(EnumHandle& e) {
        constexpr lang L{B::language};
        constexpr policy_kind pol{policy_of(^^E)};
        template for (constexpr auto en :
                      std::define_static_array(std::meta::enumerators_of(^^E))) {
            if constexpr (Resolution::class_member_participates(en, L, pol, ^^E))
                B::template add_enumerator<en, Style>(e);
        }
    }

    /** Everything a class binding contains beyond the class handle itself —
        shared by `bind_type` (a module-scope class) and `bind_nested_type` (a
        class-scoped one).

        Order matters: **nested member types register first** — a rod places
        them on the class handle just created, and pybind11 converts a later
        constructor/method *default argument* at registration time, so a nested
        enum a default argument names must already exist. Then the constructor
        set (with the no-constructor-left fail-safe), then data members /
        methods / operators (@ref bind_members).

        @tparam B     the rod.
        @tparam T     the type whose binding to fill.
        @tparam Style the name style.
        @param m   the module handle (rods without nested placement fall back to it).
        @param cls the class handle `make_class`/`make_nested_class` created. */
    template <rod B, class T, class Style, class Cls>
    static void bind_class_interior(typename B::module_type& m, Cls& cls) {
        constexpr lang L{B::language};

        // Nested member types (classes + enums declared inside T) — recursive:
        // each one's own interior runs this walk again.
        bind_nested_types<B, ^^T, Style>(m, cls);

        // Constructors, handed to the rod as ONE participating set (several
        // frameworks want them all at once — sol2's sol::constructors, LuaBridge3's
        // addConstructor). Three carriage-computed pieces:
        //  - the default constructor: constructibility is decided against the type
        //    welder actually constructs, which may be a rod-nominated substitute —
        //    a Python trampoline standing in for an abstract base — exposed as an
        //    optional `B::construction_type<T>` (fall back to T when a rod names
        //    none); a *declared* default constructor's explicit marks are honored
        //    (default_ctor_admitted);
        //  - each participating public non-copy/move constructor — resolved
        //    SYMMETRICALLY with every other member (the type's policy + the
        //    constructor's own marks, per constructor) — each gated for
        //    bindability;
        //  - for a baseless aggregate whose fields all participate, a synthesized
        //    field constructor;
        //  - the COPY constructor, as a bool only — it never binds as an init
        //    overload; its target-language spelling is the rod's (the Python
        //    rods emit __copy__/__deepcopy__ over it, the Lua rods ignore it).
        //    Marks on a declared copy constructor are honored exactly like the
        //    default constructor's (copy_ctor_admitted). Move constructors
        //    never bind at all — an include/only mark on one is a designed
        //    hard error (validate_move_ctor_marks), not a silent drop.
        detail::validate_move_ctor_marks<^^T>();
        constexpr bool has_default{
            [] {
                if constexpr (requires {
                                  typename B::template construction_type<T>;
                              })
                    return std::is_default_constructible_v<
                        typename B::template construction_type<T>>;
                else
                    return std::is_default_constructible_v<T>;
            }() &&
            detail::default_ctor_admitted<Resolution, ^^T, L>()};
        constexpr auto ctors{
            detail::ctor_group<Resolution, ^^T, L, policy_of(^^T)>()};
        template for (constexpr auto ctor : std::define_static_array(ctors)) {
            // Scope-aware gate: T's own member aliases may register a
            // parameter's type (see bind_members' Reg).
            welder::assert_callable_bindable<
                B, ctor, L, detail::scoped_registration<Resolution, ^^T>>();
        }
        constexpr bool aggregate{
            detail::aggregate_initializable<T, L, Resolution>()};
        // The fail-safe against SILENT uninstantiability: if the policy filtering
        // left T with no constructor at all, but this same resolution would have
        // admitted some under `automatic`, the emptiness came from a default (an
        // opt_in type whose constructors nobody marked) rather than a decision —
        // hard error. Explicit emptiness passes: mark::exclude-ing the
        // constructors zeroes the automatic baseline too (factory-only intent),
        // and a type automatic would also leave bare (private/deleted ctors, an
        // abstract base) was never instantiable to begin with.
        static_assert(
            has_default || aggregate || ctors.size() != 0 ||
                detail::ctor_group<Resolution, ^^T, L,
                                   policy_kind::automatic>()
                        .size() == 0,
            "welder: policy filtering left this type with NO constructor — it "
            "would be silently uninstantiable from the target language. Mark a "
            "constructor [[=welder::mark::include]] (opt_in binds only marked "
            "members, constructors included), or mark them all "
            "[[=welder::mark::exclude]] to make a factory-only surface explicit. "
            "The type is the T of this bind_type<B, T, Style> instantiation.");
        constexpr bool copyable{
            std::is_copy_constructible_v<T> &&
            detail::copy_ctor_admitted<Resolution, ^^T, L>()};
        B::template add_constructors<T, ctors, has_default, aggregate, copyable>(
            cls);

        // Data members + methods (T's own, plus flattened bases).
        bind_members<B, ^^T, ^^T, Style>(cls);

        // Method-backed properties (getter/setter marks) — resolved once per
        // welded type across the same flattening, like the operator slots.
        bind_properties<B, T, Style>(cls);

        // Operators — emitted once per welded type, across every source.
        bind_operators<B, T>(cls);
    }

    /** Emit @a T's method-backed **properties**: every accessor-marked member
        function (own + flattened bases', see `detail::collect_accessors`),
        shape-validated and paired into one `detail::property_entry` per
        property name by `detail::property_entries` — the getter authoritative,
        the setter optional (absent = read-only). Each half is gated for
        bindability like any callable; the resolved name (explicit mark name
        verbatim, else the styled-then-stripped identifier) is computed here —
        the driver owns property naming, like class/enum names — and handed to
        the rod's `add_property<T, Getter, Setter>` hook.
        @tparam B     the rod.
        @tparam T     the welded type.
        @tparam Style the name style.
        @param cls the class handle. */
    template <rod B, class T, class Style, class Cls>
    static void bind_properties(Cls& cls) {
        constexpr lang L{B::language};
        using Reg = detail::scoped_registration<Resolution, ^^T>;
        template for (constexpr auto p :
                      std::define_static_array(
                          detail::property_entries<Resolution>(^^T, L))) {
            welder::assert_callable_bindable<B, p.getter, L, Reg>();
            // The setter gates its PARAMETER only: its return value (a fluent
            // T& chains in C++) is discarded by every rod — the property
            // protocol has no slot for it — so it never faces the gate.
            if constexpr (p.setter != std::meta::info{})
                welder::assert_setter_bindable<B, p.setter, L, Reg>();
            B::template add_property<T, p.getter, p.setter>(
                cls, detail::property_bound_name<p.getter, L, Style>());
        }
    }

    /** Emit @a T's operators: member ones (own + flattened bases') and the
        **anchored free operators** of T's enclosing namespace, combined per
        `(operator, arity)` slot (`detail::operator_entries` /
        `operator_slot_set`) so a one-value-per-slot backend (the Lua rods)
        receives each slot's complete overload set in one call — a free
        `operator+` and a member `operator+` can never clobber each other, and a
        flattened base's operator now shares its slot with the derived type's
        instead of being overwritten.

        Three routes out of the combined entry list:
        - a **spaceship** group (`operator<=>`, member or free) never binds
          directly — it reaches the rod as `add_comparisons`, which synthesizes
          the four relational slots via rewritten expressions (`a < b`), skipping
          any slot an explicit participating operator already covers
          (`covered_comparison_slots`); only the *operand* types face the gate
          (the `std::*_ordering` return never crosses);
        - the free **ostream inserter** (`operator<<(std::ostream&, T)`) becomes
          the rod's stringifier (`__str__` / `__tostring`) via `add_stringifier`
          — collected separately, so its `std::ostream&` never faces the gate;
        - every other slot goes to `add_operator` when the rod exposes it
          (`special_method_name` on the slot's leader), each entry gated whole.

        @tparam B the rod.
        @tparam T the welded type.
        @param cls the class handle. */
    template <rod B, class T, class Cls>
    static void bind_operators(Cls& cls) {
        constexpr lang L{B::language};
        using Reg = detail::scoped_registration<Resolution, ^^T>;

        template for (constexpr auto fn :
                      std::define_static_array(
                          detail::operator_entries<Resolution>(^^T, L))) {
            if constexpr (std::meta::operator_of(fn) ==
                          std::meta::operators::op_spaceship) {
                if constexpr (detail::is_overload_leader<
                                  detail::operator_slot_set<Resolution>>(fn, L,
                                                                         ^^T)) {
                    constexpr auto grp{detail::overload_group<
                        detail::operator_slot_set<Resolution>, fn, L, ^^T>()};
                    template for (constexpr auto member :
                                  std::define_static_array(grp)) {
                        welder::assert_operands_bindable<B, member, ^^T, L,
                                                         Reg>();
                    }
                    B::template add_comparisons<
                        T, grp,
                        detail::covered_comparison_slots<Resolution>(^^T, L)>(
                        cls);
                }
            } else if constexpr (B::special_method_name(fn) != nullptr) {
                if constexpr (detail::is_overload_leader<
                                  detail::operator_slot_set<Resolution>>(fn, L,
                                                                         ^^T)) {
                    constexpr auto grp{detail::overload_group<
                        detail::operator_slot_set<Resolution>, fn, L, ^^T>()};
                    template for (constexpr auto member :
                                  std::define_static_array(grp)) {
                        welder::assert_callable_bindable<B, member, L, Reg>();
                    }
                    B::template add_operator<T, grp>(cls);
                }
            }
        }

        // The stringifier: at most one inserter binds (the to-string protocols
        // take no second operand to overload on; entry [0] wins, in declaration
        // order). Only T itself is gated — it is the welded type, so there is
        // nothing further to assert.
        constexpr auto strs{std::define_static_array(
            detail::stringifier_entries<Resolution>(^^T, L))};
        if constexpr (strs.size() != 0)
            B::template add_stringifier<T, strs[0]>(cls);
    }

    /** Register the participating **member types** of @a Outer onto its class
        handle @a cls — the nested-class / nested-enum walk.

        A nested type resolves like any other class member — the *outer's*
        policy plus the type's own `exclude`/`include`/`only` marks
        (`class_member_participates`), with the usual access admission — never
        via its own `weld`: nested types are interface helpers of their
        enclosing type, and the enclosing `weld` is the discovery marker.
        Deliberately skipped:
        - member type **aliases** (binding one would re-register its target;
          the alias-welding route is namespace-scope only);
        - **unions** (`is_class_type` excludes them) — unions never bind
          anywhere in welder (reading an inactive member is UB; use
          `std::variant`), so a nested one is skipped and any member *using*
          it fails the gate with the union diagnostic;
        - **unnamed** types (there is nothing to name them by — bind the
          *member* of that type or exclude it), and **incomplete** member
          types (nothing to register; a use of one also fails the gate);
        - a flattened base's nested types: a nested type registers exactly
          once, with its DECLARING class — two derived types flattening one
          mixin would otherwise register it twice (a framework load error). A
          flattened signature naming one therefore fails the gate until the
          declaring base is welded (or the member trusted/excluded).

        @tparam B     the rod.
        @tparam Outer the enclosing (welded) type being bound.
        @tparam Style the name style.
        @param m   the module handle (the flat-placement fallback scope).
        @param cls the outer type's class handle. */
    template <rod B, std::meta::info Outer, class Style, class Cls>
    static void bind_nested_types(typename B::module_type& m, Cls& cls) {
        constexpr lang L{B::language};
        constexpr auto ctx{std::meta::access_context::unchecked()};
        constexpr policy_kind pol{policy_of(Outer)};
        template for (constexpr auto mem : std::define_static_array(
                          std::meta::members_of(Outer, ctx))) {
            if constexpr (std::meta::is_type(mem) &&
                          !std::meta::is_type_alias(mem) &&
                          (std::meta::is_class_type(mem) ||
                           std::meta::is_enum_type(mem)) &&
                          std::meta::has_identifier(mem) &&
                          std::meta::is_complete_type(mem)) {
                if constexpr (detail::member_access_admitted<Resolution>(
                                  mem, L, Outer) &&
                              Resolution::class_member_participates(mem, L, pol,
                                                                    Outer)) {
                    if constexpr (std::meta::is_enum_type(mem))
                        bind_nested_enum<B, typename [:mem:], Style>(m, cls);
                    else
                        bind_nested_type<B, typename [:mem:], Style>(m, cls);
                }
            } else if constexpr (std::meta::is_type_alias(mem) &&
                                 std::meta::has_identifier(mem) &&
                                 (std::meta::is_class_type(
                                      std::meta::dealias(mem)) ||
                                  std::meta::is_enum_type(
                                      std::meta::dealias(mem))) &&
                                 std::meta::is_complete_type(
                                     std::meta::dealias(mem))) {
                // A MEMBER TYPE ALIAS: participates by the member rules (the
                // outer's policy + the alias's own marks), with the bindability
                // gate as the register-or-skip arbiter — a target the gate
                // already passes (natively castable, a bindable STL wrapper,
                // welded, otherwise registered, or trusted) converts without
                // this registration, so registering it again would be redundant
                // or an outright duplicate. The rule registers exactly the
                // types that otherwise could not cross the boundary — nested
                // under the outer, named by the alias (its weld_as → the
                // target's → the styled identifier). Under greedy resolution
                // every complete type passes the gate, so member aliases never
                // participate in a tack weld, by construction.
                static_assert(
                    welder::member_alias_marks_admissible(mem),
                    "welder: only weld_as / exclude / include / only may be "
                    "attached to a member type alias; policy and doc marks "
                    "belong on the target type, and weld / trust_bindable have "
                    "no meaning here (participation follows the outer's policy "
                    "and the bindability gate)");
                if constexpr (detail::member_access_admitted<Resolution>(
                                  mem, L, Outer) &&
                              Resolution::class_member_participates(mem, L, pol,
                                                                    Outer) &&
                              !welder::bindable<
                                  B, typename [:std::meta::dealias(mem):], L,
                                  Resolution>()) {
                    static_assert(
                        detail::sole_member_alias_of_target<Resolution, Outer,
                                                            mem>(L, pol),
                        "welder: two member aliases in this class weld the SAME "
                        "target type — each target may be registered under "
                        "exactly one name; mark::exclude one of them");
                    if constexpr (std::meta::is_enum_type(
                                      std::meta::dealias(mem)))
                        bind_nested_enum<B, typename [:std::meta::dealias(mem):],
                                         Style>(
                            m, cls,
                            detail::alias_bound_name<mem, L, Style,
                                                     ent_kind::enum_>());
                    else
                        bind_nested_type<B, typename [:std::meta::dealias(mem):],
                                         Style, mem>(
                            m, cls, detail::alias_bound_name<mem, L, Style>());
                }
            }
        }
    }

    /** Register nested class @a T onto the class handle of its enclosing type —
        `bind_type`'s class-scope sibling.

        Same interior (constructors, fail-safes, members, its *own* nested
        types — so nesting recurses), differing only in where the class handle
        is created: a rod implementing the optional `make_nested_class` places
        the new class under @a outer (the Python rods pass the enclosing class
        as the registration scope, yielding `module.Outer.Inner`); one that
        doesn't falls back to the module-scope `make_class` — a flat sibling
        under its usual resolved name, so two outers' same-named nested types
        then collide (rename with `weld_as`, or implement the hook).

        No `participates` assert here: participation was the caller's member
        resolution (a nested type needs no `weld`).

        @tparam B     the rod.
        @tparam T     the nested class type.
        @tparam Style the name style.
        @tparam Decl  the declaring entity when it differs from `^^T` — the
                      MEMBER ALIAS a type was welded through (the alias is the
                      one spellable name an aliased specialization has, which
                      the text-emitting rods need); a null reflection for the
                      declared-nested route.
        @param m     the module handle (the flat-placement fallback scope).
        @param outer the enclosing type's class handle.
        @param name  the bound name (an alias-resolved override), or `nullptr`
                     to resolve @a T's `weld_as`/styled name. */
    template <rod B, class T, class Style, std::meta::info Decl = std::meta::info{},
              class OuterCls>
    static void bind_nested_type(typename B::module_type& m, OuterCls& outer,
                                 const char* name = nullptr) {
        constexpr lang L{B::language};
        // name_of_or, not `name ? name : name_of<…>`: the consteval fallback
        // must only be compiled when T is statically nameable — an alias-welded
        // specialization has no identifier and always arrives with an override.
        const char* cls_name{
            welder::name_of_or<^^T, L, Style, ent_kind::class_>(name)};
        auto cls{make_nested_class_of<B, T, Decl,
                                      Resolution::template native_bases<^^T, L>()>(
            m, outer, cls_name, welder::doc_of<^^T>())};
        bind_class_interior<B, T, Style>(m, cls);
        // A second OPTIONAL rod hook, after the interior: a rod whose class
        // handle re-opens the class by name/path (LuaBridge3) cannot move the
        // class under the outer at creation time — every later add_* call would
        // re-open a key that no longer exists — so it finalizes the placement
        // here, once the interior (nested types included, so innermost first)
        // is fully registered.
        if constexpr (requires {
                          B::template finish_nested_class<T>(m, outer, cls,
                                                             cls_name);
                      })
            B::template finish_nested_class<T>(m, outer, cls, cls_name);
    }

    /** Register nested enum @a E onto the class handle of its enclosing type —
        `bind_enum`'s class-scope sibling (see @ref bind_nested_type for the
        shared rationale; the fallback for a rod without `make_nested_enum` is
        the module-scope `make_enum`).
        @tparam B     the rod.
        @tparam E     the nested enum type.
        @tparam Style the name style.
        @param m     the module handle (the flat-placement fallback scope).
        @param outer the enclosing type's class handle.
        @param name  the bound name (an alias-resolved override), or `nullptr`
                     to resolve @a E's `weld_as`/styled name. */
    template <rod B, class E, class Style, class OuterCls>
    static void bind_nested_enum(typename B::module_type& m, OuterCls& outer,
                                 const char* name = nullptr) {
        constexpr lang L{B::language};
        const char* enum_name{
            welder::name_of_or<^^E, L, Style, ent_kind::enum_>(name)};
        auto e{make_nested_enum_of<B, E>(m, outer, enum_name,
                                         welder::doc_of<^^E>())};
        emit_enumerators<B, E, Style>(e);
        B::template finish_enum<E>(e);
    }

    /** Call the rod's nested-class primitive when it declares one, else fall
        back to the module-scope factory (flat placement). A static helper, not
        a lambda in `bind_nested_type`, for the same P2564 reason as
        @ref make_class_of.

        A spelling-aware rod may declare the extended, declaring-entity-aware
        form `make_nested_class<T, Decl, Bases>` (preferred when @a Decl is set
        — the member alias an unnameable specialization was welded through);
        the flat fallback likewise threads @a Decl into `make_class_of`, so a
        text-emitting rod's extended `make_class` receives it too.
        @tparam B     the rod.
        @tparam T     the nested class type.
        @tparam Decl  the declaring member alias, or a null reflection.
        @tparam Bases the native base reflections.
        @param m        the module handle.
        @param outer    the enclosing type's class handle.
        @param cls_name the resolved target-language name.
        @param doc      the class docstring, or `nullptr`.
        @return the rod's class handle for @a T. */
    template <rod B, class T, std::meta::info Decl, auto Bases, class OuterCls>
    static auto make_nested_class_of(typename B::module_type& m, OuterCls& outer,
                                     const char* cls_name, const char* doc) {
        constexpr std::meta::info decl{Decl == std::meta::info{} ? ^^T : Decl};
        if constexpr (requires {
                          B::template make_nested_class<T, decl, Bases>(
                              m, outer, cls_name, doc,
                              std::make_index_sequence<Bases.size()>{});
                      })
            return B::template make_nested_class<T, decl, Bases>(
                m, outer, cls_name, doc,
                std::make_index_sequence<Bases.size()>{});
        else if constexpr (requires {
                               B::template make_nested_class<T, Bases>(
                                   m, outer, cls_name, doc,
                                   std::make_index_sequence<Bases.size()>{});
                           })
            return B::template make_nested_class<T, Bases>(
                m, outer, cls_name, doc,
                std::make_index_sequence<Bases.size()>{});
        else
            return make_class_of<B, T, decl, Bases>(m, cls_name, doc);
    }

    /** `make_nested_class_of`'s enum counterpart: the rod's `make_nested_enum`
        when present, else the module-scope `make_enum`.
        @tparam B the rod.
        @tparam E the nested enum type.
        @param m     the module handle.
        @param outer the enclosing type's class handle.
        @param name  the resolved target-language name.
        @param doc   the enum docstring, or `nullptr`.
        @return the rod's enum handle for @a E. */
    template <rod B, class E, class OuterCls>
    static auto make_nested_enum_of(typename B::module_type& m, OuterCls& outer,
                                    const char* name, const char* doc) {
        if constexpr (requires {
                          B::template make_nested_enum<E>(m, outer, name, doc);
                      })
            return B::template make_nested_enum<E>(m, outer, name, doc);
        else
            return B::template make_enum<E>(m, name, doc);
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
        emit_enumerators<B, E, Style>(e);
        B::template finish_enum<E>(e);
        return e;
    }

    /** Call the rod's class-creation primitive, preferring the extended
        declaring-entity-aware form when the rod provides one.

        @a Decl is the entity the type was *declared through* — `^^T`, or the
        namespace-scope alias welding a class-template specialization. A helper
        (not a lambda in `bind_type`): `std::meta::info` is consteval-only, so a
        lambda referencing such a constexpr local escalates to consteval (P2564)
        and cannot take the runtime module handle; template parameters do not.
        @tparam B     the rod.
        @tparam T     the type to bind.
        @tparam Decl  the declaring entity handed to a spelling-aware rod.
        @tparam Bases the native base reflections.
        @param m        the module handle to register onto.
        @param cls_name the resolved target-language name.
        @param doc      the class docstring, or `nullptr`.
        @return the rod's class handle. */
    template <rod B, class T, std::meta::info Decl, auto Bases>
    static auto make_class_of(typename B::module_type& m, const char* cls_name,
                              const char* doc) {
        if constexpr (requires {
                          B::template make_class<T, Decl, Bases>(
                              m, cls_name, doc,
                              std::make_index_sequence<Bases.size()>{});
                      })
            return B::template make_class<T, Decl, Bases>(
                m, cls_name, doc, std::make_index_sequence<Bases.size()>{});
        else
            return B::template make_class<T, Bases>(
                m, cls_name, doc, std::make_index_sequence<Bases.size()>{});
    }

    /** Reflect over @a T and register it via rod @a B onto module @a m.

        Emits:
        - native inheritance from @a T's native ancestors (per @a Resolution; each a
          base of the class handle — bind those bases separately, before @a T);
        - @a T's own **nested member types** (classes + enums, recursively) that
          resolve as bound — like any member, under @a T's policy + their own
          marks, no `weld` of their own (@ref bind_nested_types); a rod with the
          optional `make_nested_class`/`make_nested_enum` primitives places them
          under @a T's binding (Python: `module.T.Inner`), others fall back to
          flat module-scope placement;
        - the default constructor (if any), each public non-copy/move constructor, and
          — for a baseless aggregate whose fields all bind — a synthesized field
          constructor;
        - data members / methods / operators that resolve as bound (public, plus
          protected where `detail::member_access_admitted` admits them — see
          `policy::weld_protected`; private never), plus the eligible members of
          every flattened (non-native) public base.

        @tparam B    the rod.
        @tparam T    the type to bind.
        @tparam Decl the declaring entity when it differs from `^^T` — the
                     namespace-scope alias a class-template specialization was
                     welded through (set by `bind_namespace`'s alias branch, which
                     has already resolved participation); a null reflection for
                     the direct route.
        @param m    the module handle to register onto.
        @param name the target name (used verbatim, beating any `weld_as`), or
                    `nullptr` to resolve @a T's `weld_as`/styled name.
        @return the rod's class handle, so callers can chain further registrations.
    */
    template <rod B, class T, class Style = naming::none,
              std::meta::info Decl = std::meta::info{}>
    static auto bind_type(typename B::module_type& m, const char* name = nullptr) {
        constexpr lang L{B::language};
        static_assert(
            !std::meta::is_union_type(std::meta::dealias(^^T)),
            "welder: unions cannot be welded — C++ has no way to observe which "
            "union member is active, so generated member accessors would read "
            "inactive members (undefined behavior). Use std::variant instead "
            "(converted natively by every rod), or expose the union through "
            "safe accessor functions on an enclosing type. To hand-register it "
            "with the backend yourself, vouch for the uses via "
            "welder::trust_bindable.");
        // Decl is the *declaring* entity when it differs from ^^T: the namespace-
        // scope alias through which a class-template specialization was welded
        // (bind_namespace's alias branch, which has already resolved participation
        // — the alias's own `weld` counts there, so ^^T alone can't be re-checked).
        static_assert(Decl != std::meta::info{} || Resolution::participates(^^T, L),
                      "welder: weld_type<T>: T is not welded for this backend's "
                      "language; annotate it with [[=welder::weld(...)]]");
        constexpr auto ctx{std::meta::access_context::unchecked()};

        const char* cls_name{
            welder::name_of_or<^^T, L, Style, ent_kind::class_>(name)};

        // Native bases → bases of the class handle; the user binds them first.
        // A text-emitting rod that must *spell* the type in C++ (the trampoline
        // generator) declares the extended, declaring-entity-aware make_class; it
        // receives Decl — the alias, the one C++ name a specialization has — or
        // ^^T itself for a directly-declared class. Detected via requires, so the
        // runtime rods keep the plain form (they need only T + the bound name).
        constexpr auto bases{Resolution::template native_bases<^^T, L>()};
        auto cls{make_class_of<B, T, (Decl == std::meta::info{} ? ^^T : Decl),
                               bases>(m, cls_name, welder::doc_of<^^T>())};

        // Nested member types, constructors, data members / methods / operators
        // (shared with bind_nested_type — nesting recurses through it).
        bind_class_interior<B, T, Style>(m, cls);

        return cls;
    }

    /** Register free function @a Fn — with its participating overload siblings —
        as a module-level function of @a m.

        The semi-manual leaf: gate then emit one *name*, without walking a
        namespace. @a Fn (which resolves the group's target name, and is welded
        even if its own marks would resolve it out — the explicit call is the
        stronger statement of intent) must be admitted by @a Resolution for @a B's
        language; its participating same-name siblings join the group, keeping the
        call consistent with the namespace walk on every rod (a
        one-value-per-name framework registers the complete set). Every group
        member's signature runs the bindability gate.

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
        static_assert(!welder::has_accessor_mark(Fn),
                      "welder: weld_function<Fn>: Fn carries a getter/setter "
                      "mark, but properties are a class surface — the marks "
                      "apply to member functions only");
        constexpr auto grp{detail::manual_function_group<Resolution, Fn, L>()};
        template for (constexpr auto member : std::define_static_array(grp)) {
            welder::assert_callable_bindable<B, member, L, Resolution>();
        }
        // Forward the rod's handle (the bound function object, where the
        // framework has one); a void-returning rod makes this void too.
        return B::template add_function<grp, Style>(m, name);
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
            // The alias branch must come first: type predicates (is_class_type)
            // look through an alias, so the class branch below would swallow it.
            if constexpr (std::meta::is_type_alias(mem)) {
                if constexpr (welder::names_template_specialization(mem)) {
                    // The one way an *instantiation* enters a sweep (members_of
                    // enumerates the template, never its specializations): the
                    // alias is both the C++ spelling and the target-language name.
                    static_assert(
                        welder::alias_marks_admissible(mem),
                        "welder: only weld / weld_as may be attached to a "
                        "namespace-scope alias; every other mark belongs on the "
                        "class template, where it applies to all instantiations");
                    if constexpr (Resolution::alias_participates(mem, L, pol, Ns)) {
                        static_assert(
                            detail::sole_alias_of_target<Resolution, Ns, mem>(L,
                                                                              pol),
                            "welder: two aliases in this namespace weld the SAME "
                            "template specialization — each specialization may be "
                            "welded under exactly one name");
                        bind_type<B, typename [:mem:], Style, mem>(
                            m, detail::alias_bound_name<mem, L, Style>());
                    }
                } else {
                    // An alias to a plain (non-template) type: binding it would
                    // register the type a second time under the alias name — a
                    // welded target makes the alias a likely mistake, diagnosed
                    // rather than silently skipped. (Rename with weld_as instead.)
                    static_assert(
                        !((std::meta::is_class_type(std::meta::dealias(mem)) ||
                           std::meta::is_enum_type(std::meta::dealias(mem))) &&
                          welder::alias_welded_for(mem, L)),
                        "welder: a namespace-scope alias to a welded NON-template "
                        "type would bind the type twice (it already binds under "
                        "its own name); remove the alias's weld, or rename the "
                        "type with [[=welder::weld_as(...)]]");
                }
            } else if constexpr (std::meta::is_type(mem) &&
                                 std::meta::is_class_type(mem)) {
                if constexpr (Resolution::member_participates(mem, L, pol, Ns))
                    bind_type<B, typename [:mem:], Style>(m, nullptr);
            } else if constexpr (std::meta::is_type(mem) && std::meta::is_enum_type(mem)) {
                if constexpr (Resolution::member_participates(mem, L, pol, Ns))
                    bind_enum<B, typename [:mem:], Style>(m, nullptr);
            } else if constexpr (std::meta::is_type(mem) &&
                                 std::meta::is_union_type(mem)) {
                // Unions never bind (reading an inactive member is UB — see
                // assert_bindable's union diagnostic). An unmarked union in a
                // swept namespace is simply skipped — its uses still fail the
                // gate — but a `weld` mark on one is an explicit attempt,
                // diagnosed loudly rather than silently ignored.
                static_assert(
                    !welder::welded_for(mem, L),
                    "welder: a union in this namespace carries "
                    "[[=welder::weld(...)]], but unions cannot be welded — C++ "
                    "has no way to observe which union member is active, so "
                    "generated accessors would read inactive members (undefined "
                    "behavior). Use std::variant instead (converted natively by "
                    "every rod), or expose the union through safe accessor "
                    "functions; hand-register it with the backend yourself via "
                    "welder::trust_bindable on its uses.");
            } else if constexpr (std::meta::is_function(mem) &&
                                 !std::meta::is_operator_function(mem)) {
                // Free functions bind as whole overload groups, emitted at the
                // group's first participating overload (see bind_members).
                // OPERATOR functions are deliberately not functions here: a free
                // operator is part of its anchor type's interface (ADL), so the
                // type's own binding sweeps it (bind_operators) — and it has no
                // identifier for a module-level name anyway.
                static_assert(
                    !welder::has_accessor_mark(mem),
                    "welder: a getter/setter mark sits on a NAMESPACE-SCOPE "
                    "function, but properties are a class surface — the marks "
                    "apply to member functions only. Bind the free function as "
                    "an ordinary module function (drop the mark), or wrap the "
                    "pair in a class.");
                if constexpr (Resolution::member_participates(mem, L, pol, Ns)) {
                    if constexpr (detail::is_overload_leader<
                                      detail::function_overload_set<Resolution>>(
                                      mem, L, Ns)) {
                        constexpr auto grp{detail::overload_group<
                            detail::function_overload_set<Resolution>, mem, L,
                            Ns>()};
                        template for (constexpr auto member :
                                      std::define_static_array(grp)) {
                            welder::assert_callable_bindable<B, member, L,
                                                             Resolution>();
                        }
                        B::template add_function<grp, Style>(m);
                    }
                }
            } else if constexpr (std::meta::is_variable(mem)) {
                if constexpr (Resolution::member_participates(mem, L, pol, Ns)) {
                    welder::assert_member_bindable<B, mem, L, Resolution>();
                    B::template add_variable<mem, Style>(m, session);
                }
            } else if constexpr (std::meta::is_namespace(mem)) {
                // A nested namespace resolves like a leaf under the parent policy (but
                // is never welded): it becomes a submodule when it holds participating
                // content (then resolved under its *own* policy).
                if constexpr (Resolution::namespace_participates(mem, L, pol, Ns)) {
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
    consuming a third-party library that carries no welder annotations. Public
    members only, like the stitch default; to tack a library's **protected**
    members too (it cannot carry the `policy::weld_protected` annotation), use
    `carriages::basic_carriage<carriages::greedy_resolution<true>>`. @see
    welder::carriages::greedy_resolution for the exact rules and caveats. */
using tack_welding_carriage =
    carriages::basic_carriage<carriages::greedy_resolution<>>;

/** The default carriage — an alias for @ref welder::stitch_welding_carriage. */
using carriage = stitch_welding_carriage;

} // namespace welder