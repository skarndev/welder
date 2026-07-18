#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <sstream> // std::ostream (the stringifier-shape test) + stringify's stream
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <welder/diag.hpp>    // the consteval diagnostics (stale_hook_signature)
#include <welder/reflect.hpp> // resolution: welded_for / member_bound / public_bases

/** @file
    Backend-agnostic *selection* layer: the reflection predicates and selectors
    that decide **what** participates in a binding — which constructors, methods,
    operators, data members, namespace entities and base classes are eligible, and
    what their parameter names/types are.

    It answers "what", never "how": no backend API appears here, so every backend
    (pybind11, nanobind, sol2) shares this vocabulary and only supplies the emission
    primitives.

    @note Like `<welder/reflect.hpp>`, this depends on the welder vocabulary
    (`lang`, `policy_kind`, …) but does NOT include `<welder/annotations.hpp>`:
    provide the vocabulary first (`#include <welder/vocabulary.hpp>`), then this.
*/

namespace welder::inline v0::detail {

// --- function / parameter introspection -------------------------------------

/** A function's parameter *types*, as a static array of reflections.

    Usable as a non-type template argument so it can be spliced back into a pack.
    @tparam Fn a reflection of the function.
    @return an array of the parameter type reflections, in order.
*/
template <std::meta::info Fn>
consteval auto param_types() {
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    std::array<std::meta::info, n> types{};
    std::size_t i{0};
    for (auto p : std::meta::parameters_of(Fn))
        types[i++] = std::meta::type_of(p);
    return types;
}

/** A function's parameter *names*, in order.

    @tparam Fn a reflection of the function.
    @return an array of static-storage C strings, one per parameter, or `nullptr`
            for an unnamed one.
*/
template <std::meta::info Fn>
consteval auto param_names() {
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    std::array<const char*, n> names{};
    std::size_t i{0};
    for (auto p : std::meta::parameters_of(Fn))
        names[i++] = std::meta::has_identifier(p)
                         ? std::define_static_string(std::meta::identifier_of(p))
                         : nullptr;
    return names;
}

/** A `keep_alive` lifetime dependency: a (nurse, patient) index pair. */
struct keep_alive_pair {
    unsigned nurse;   /**< The keeper whose collection bounds the dependency. */
    unsigned patient; /**< The dependant kept alive until the nurse is collected. */
};

/** The `keep_alive` dependencies declared on @a Fn, in declaration order.

    Materialized as a fixed-size static array so a rod can splice each pair back as
    a `keep_alive<nurse, patient>()` call-policy template argument. Empty when @a Fn
    carries no `keep_alive` annotation.
    @tparam Fn a reflection of the callable.
    @return an array of the (nurse, patient) pairs. */
template <std::meta::info Fn>
consteval auto keep_alive_pairs() {
    constexpr std::size_t n{
        std::meta::annotations_of_with_type(Fn, ^^keep_alive_spec).size()};
    std::array<keep_alive_pair, n> out{};
    if constexpr (n != 0) {
        std::size_t i{0};
        for (auto a : std::meta::annotations_of_with_type(Fn, ^^keep_alive_spec)) {
            auto s{std::meta::extract<keep_alive_spec>(a)};
            out[i++] = {s.nurse, s.patient};
        }
    }
    return out;
}

/** Whether every parameter of @a Fn carries an identifier.

    Keyword-argument naming is all-or-nothing across backends, so an unnamed
    parameter means positional.
    @tparam Fn a reflection of the function.
    @return `true` iff all parameters are named.
*/
template <std::meta::info Fn>
consteval bool all_params_named() {
    for (auto p : std::meta::parameters_of(Fn))
        if (!std::meta::has_identifier(p))
            return false;
    return true;
}

// --- constructor / method / operator eligibility ----------------------------

/** A non-default, non-copy/move public constructor a backend should expose.

    The default constructor is handled separately (it may be implicit, hence not a
    member), as is the copy constructor — it has a target-language spelling of its
    own (@ref copy_ctor_admitted; Python `__copy__`/`__deepcopy__`), never an init
    overload. Move constructors never bind at all (no target language has move
    semantics; a marked one is diagnosed — @ref validate_move_ctor_marks).
    @param c a reflection of the constructor.
    @return `true` iff @a c is public, non-deleted, not copy/move, and takes at
            least one parameter.
*/
consteval bool is_bindable_constructor(std::meta::info c) {
    return std::meta::is_constructor(c) && std::meta::is_public(c) &&
           !std::meta::is_deleted(c) && !std::meta::is_copy_constructor(c) &&
           !std::meta::is_move_constructor(c) &&
           !std::meta::parameters_of(c).empty();
}

/** The *shape* of a bindable method: a plain non-private member function.

    Shape only — whether it *participates* is the resolution's decision (its
    `class_member_participates` hook; `member_bound` under the shipped
    resolutions), and whether its access level is admitted is
    @ref member_access_admitted's (public always, protected under
    `policy::weld_protected` / the resolution's `protected_participates` hook);
    both are composed by the carriage. **Private** members are rejected here,
    in the shape — exposing a private member is a violation of welder's design,
    so no resolution can readmit one. Special members, destructors and operators
    are skipped (operators are classified separately).
    @param f a reflection of the member function.
    @return `true` iff @a f is a non-private, non-deleted, non-special member
            function.
*/
consteval bool is_method_candidate(std::meta::info f) {
    return std::meta::is_function(f) && !std::meta::is_constructor(f) &&
           !std::meta::is_special_member_function(f) &&
           !std::meta::is_destructor(f) && !std::meta::is_operator_function(f) &&
           !std::meta::is_private(f) && !std::meta::is_deleted(f);
}

/** The *shape* of a bindable member operator.

    Shape only, like @ref is_method_candidate — participation is the resolution's
    call, access admission @ref member_access_admitted's, and private is
    rejected here for the same design reason. Whether it maps to something in
    the target language — and under what name — is a backend decision (see
    `rod::special_method_name`).
    @param f a reflection of the operator function.
    @return `true` iff @a f is a non-private, non-deleted, non-special member
            operator.
*/
consteval bool is_operator_candidate(std::meta::info f) {
    return std::meta::is_function(f) && std::meta::is_operator_function(f) &&
           !std::meta::is_special_member_function(f) &&
           !std::meta::is_private(f) && !std::meta::is_deleted(f);
}

// --- freestanding (namespace-scope) operators --------------------------------
//
// A free operator is part of a type's interface exactly as a member one is (C++
// finds it by ADL), so welder sweeps a welded type's enclosing namespace for
// operators *anchored* on the type — one operand IS the type — and folds them
// into the type's binding. Hidden friends are the one shape reflection cannot
// see (P2996 enumerates neither a class's friends nor ADL): move them to
// namespace scope, or bind them by hand on the returned class handle.

/** Whether type reflection @a t is exactly the (welded) type @a type once
    stripped of cv/ref qualifiers and aliases. The anchor test — exact identity,
    never a base: a base-anchored free operator rides through the target
    language's class inheritance when the base is welded.
    @param t    a type reflection (possibly qualified / aliased).
    @param type the plain type to compare against.
    @return `true` iff @a t decays to @a type. */
consteval bool decays_to(std::meta::info t, std::meta::info type) {
    return std::meta::dealias(std::meta::remove_cvref(t)) ==
           std::meta::dealias(type);
}

/** The *shape* of a bindable freestanding operator: a non-deleted
    namespace-scope operator function. Participation is still the resolution's
    call (under the anchor type's policy, with the operator's own marks).
    @param f a reflection of the function.
    @return `true` iff @a f is a non-deleted namespace-scope operator. */
consteval bool is_free_operator_candidate(std::meta::info f) {
    return std::meta::is_function(f) && std::meta::is_operator_function(f) &&
           !std::meta::is_class_member(f) && !std::meta::is_deleted(f);
}

/** Whether @a f is the stream-inserter ("stringifier") shape for @a type:
    `operator<<(std::ostream&, T)`. Never bound as a shift slot — a rod maps it
    to the target's to-string protocol (Python `__str__`, Lua `__tostring`), so
    the `std::ostream&` parameter is deliberately not gated for bindability.
    @param f    a reflection of the function.
    @param type the anchor type.
    @return `true` iff @a f is @a type's ostream inserter. */
consteval bool is_stringifier_for(std::meta::info f, std::meta::info type) {
    if (!is_free_operator_candidate(f) ||
        std::meta::operator_of(f) != std::meta::operators::op_less_less)
        return false;
    auto ps{std::meta::parameters_of(f)};
    if (ps.size() != 2)
        return false;
    const auto p0{std::meta::type_of(ps[0])};
    return std::meta::is_lvalue_reference_type(p0) &&
           decays_to(p0, ^^std::ostream) &&
           decays_to(std::meta::type_of(ps[1]), type);
}

/** Whether free operator @a f is **anchored** on @a type: some parameter decays
    to exactly @a type (an rvalue-reference operand disqualifies — it consumes
    the object, which no binding call form expresses). The stringifier shape is
    excluded (it has its own route).
    @param f    a reflection of the function.
    @param type the candidate anchor type.
    @return `true` iff @a f participates in @a type's operator sweep. */
consteval bool free_operator_anchored(std::meta::info f, std::meta::info type) {
    if (!is_free_operator_candidate(f) || is_stringifier_for(f, type))
        return false;
    for (auto p : std::meta::parameters_of(f)) {
        const auto t{std::meta::type_of(p)};
        if (decays_to(t, type) && !std::meta::is_rvalue_reference_type(t))
            return true;
    }
    return false;
}

/** Whether anchored free operator @a f binds **reflected** for @a type: @a type
    is the *right* operand and the left is something else (`operator*(double,
    Vec)`). The Python rods bind such an entry under the reflected dunder
    (`__rmul__`, or the operand-swapped comparison); the Lua rods need no
    distinction (a metamethod receives the operands as passed).
    @param f    a reflection of the anchored operator.
    @param type the anchor type.
    @return `true` iff @a type is only the right operand. */
consteval bool free_operator_reflected(std::meta::info f, std::meta::info type) {
    if (std::meta::is_class_member(f))
        return false;
    auto ps{std::meta::parameters_of(f)};
    return ps.size() == 2 && !decays_to(std::meta::type_of(ps[0]), type);
}

/** Split slot group @a Fns by @ref free_operator_reflected: the entries whose
    reflectedness equals @a Reflected, in group order. A rod whose framework
    registers by exact signature shape (LuaBridge3) uses this to route the
    direct (anchor-on-the-left) entries through its typed registration and the
    reflected ones through a raw fallback.
    @tparam Fns       the slot group (a static array of reflections).
    @tparam Type      the anchor type's reflection.
    @tparam Reflected which partition to keep.
    @return the matching entries as a static array. */
template <auto Fns, std::meta::info Type, bool Reflected>
consteval auto partition_reflected() {
    constexpr std::size_t n{[] {
        std::size_t c{0};
        for (auto f : Fns)
            if (free_operator_reflected(f, Type) == Reflected)
                ++c;
        return c;
    }()};
    std::array<std::meta::info, n> out{};
    // Guard the fill: std::array<T, 0>::operator[] is not consteval.
    if constexpr (n != 0) {
        std::size_t i{0};
        for (auto f : Fns)
            if (free_operator_reflected(f, Type) == Reflected)
                out[i++] = f;
    }
    return out;
}

/** The nearest enclosing **namespace** of @a type — the scope its anchored free
    operators are swept from (for a nested class, the namespace enclosing the
    outermost class, mirroring where C++ requires such operators to live).
    @param type a reflection of the type.
    @return the enclosing namespace reflection. */
consteval std::meta::info enclosing_namespace(std::meta::info type) {
    auto p{std::meta::parent_of(type)};
    while (!std::meta::is_namespace(p))
        p = std::meta::parent_of(p);
    return p;
}

/** The operand a comparison synthesized from spaceship overload @a f takes: the
    parameter type that is not the anchor @a type itself — or @a type for the
    homogeneous form.
    @param f    a reflection of an `operator<=>` overload (member or anchored free).
    @param type the anchor type.
    @return the operand's declared type reflection. */
consteval std::meta::info comparison_operand(std::meta::info f,
                                             std::meta::info type) {
    for (auto p : std::meta::parameters_of(f))
        if (!decays_to(std::meta::type_of(p), type))
            return std::meta::type_of(p);
    return type; // homogeneous: every operand is the type itself
}

/** Whether spaceship group @a Fns contains a heterogeneous overload (an operand
    that is not @a Type itself) — the trigger for a reversed-operand fallback on
    rods whose framework registers by exact signature shape.
    @tparam Fns  the spaceship group.
    @tparam Type the anchor type's reflection.
    @return `true` iff some overload compares against another type. */
template <auto Fns, std::meta::info Type>
consteval bool has_heterogeneous_comparison() {
    for (auto f : Fns)
        if (!decays_to(comparison_operand(f, Type), Type))
            return true;
    return false;
}

/** The rewritten-expression comparison a rod binds for a type whose C++
    comparisons come from `operator<=>`: plain `a < b` (etc.), so C++'s own
    operator-rewriting rules ([over.match.oper]) pick the spaceship overload —
    heterogeneous and reversed operands included — and the target language sees
    exactly what a C++ caller sees. The four relational forms are synthesized;
    `==` never is (C++ itself only rewrites `==` from `operator==`, which a
    *defaulted* spaceship implicitly declares — and that member then binds
    through the ordinary operator path).
    @tparam A the left operand type.
    @tparam B the right operand type.
    @tparam S which relational slot this wrapper fills. */
enum class cmp_slot : std::uint8_t { lt = 0, le = 1, gt = 2, ge = 3 };
template <class A, class B, cmp_slot S>
struct synthesized_comparison {
    /** Evaluate `a OP b` through C++'s rewriting rules.
        @param a the left operand. @param b the right operand.
        @return the comparison result. */
    static bool call(const A& a, const B& b) {
        if constexpr (S == cmp_slot::lt)
            return a < b;
        else if constexpr (S == cmp_slot::le)
            return a <= b;
        else if constexpr (S == cmp_slot::gt)
            return a > b;
        else
            return a >= b;
    }
};

/** The stringifier wrapper every runtime rod binds for a swept ostream inserter
    (see `is_stringifier_for`): run the specific participating overload @a Fn
    into a string stream and hand back the text — the body of Python's `__str__`
    and Lua's `__tostring`.
    @tparam T  the welded type.
    @tparam Fn the inserter's reflection.
    @param self the object to stringify.
    @return the inserted text. */
template <class T, std::meta::info Fn>
std::string stringify(const T& self) {
    std::ostringstream os{};
    [:Fn:](os, self);
    return os.str();
}

/** Is @a mem's *access level* admitted for binding under @a Resolution?

    The access counterpart of `class_member_participates`: public members are
    always admitted; **private** members never are (hard-wired here, before the
    resolution is consulted — exposing a private member is a violation of
    welder's design, not a policy a resolution may choose); **protected**
    members are arbitrated by the resolution's optional
    `protected_participates(mem, L, bound_into)` hook, falling back — when the
    resolution declares none — to the declaring class's
    `policy::weld_protected` annotation (@ref welder::protected_welded). The
    fallback keeps the annotation honored under any bespoke resolution unless
    it deliberately takes the decision over; the tack-welding
    `greedy_resolution` exposes it as a template knob for libraries that
    cannot be annotated.

    @a bound_into is the entity whose binding *receives* the member — the
    welded (most-derived) type, held fixed through the base-flattening
    recursion, or the enum for an enumerator. It equals
    `parent_of(mem)` except when a non-welded base's member is flattened onto
    a derived binding — exactly the case a bespoke hook may want to key on
    ("admit this mixin's protected members, but only into `Derived`"). The
    shipped resolutions ignore it (the declaring-class annotation rule).

    A protected member admitted here still resolves through the normal
    machinery (policy kind, marks, overload grouping, the bindability gate) —
    admission only makes it *visible*. Emission needs no publicist and no
    generated wrapper: access control applies to *names*, and welder binds
    through spliced pointers-to-member (`&[:mem:]`), which P2996 exempts from
    access checking once the reflection was obtained (welder queries with
    `access_context::unchecked()` throughout).

    @tparam Resolution the carriage's resolution.
    @param mem        a reflection of the class member.
    @param L          the target language.
    @param bound_into the entity whose binding receives the member.
    @return `true` iff @a mem's access level admits it for @a L.
*/
template <class Resolution>
consteval bool member_access_admitted(std::meta::info mem, lang L,
                                      std::meta::info bound_into) {
    if (std::meta::is_public(mem))
        return true;
    if (!std::meta::is_protected(mem))
        return false; // private (or inaccessible): out by design, always
    if constexpr (requires {
                      Resolution::protected_participates(mem, L, bound_into);
                  }) {
        return Resolution::protected_participates(mem, L, bound_into);
    } else if constexpr (requires {
                             Resolution::protected_participates(mem, L);
                         }) {
        // A pre-bound_into hook: hard-error rather than silently ignore it.
        throw ::welder::diag::stale_hook_signature{};
    } else {
        return ::welder::protected_welded(std::meta::parent_of(mem), L);
    }
}

/** Splice-based accessors for data member @a Mem — the pointer-to-member-free
    route the rods bind a **protected** field through.

    [gcc-16 workaround, isolated here] gcc-16 access-checks a *dependent*
    `&[:Mem:]` on a protected data member ("protected within this context"),
    although the identical splice passes in a non-dependent context and the
    dependent `&[:Fn:]` on a protected member *function* passes too — under
    P2996 none of them should be checked (access applies to *names*; a splice
    is not a name, and the access decision was already taken by the
    `access_context` at query time). `std::meta::extract<F C::*>(Mem)` trips
    the same check. The member-*access* splice `obj.[:Mem:]` does not, so a
    protected field binds as a property over these two accessors instead of a
    `def_readwrite`-style pointer-to-member. Both spellings are standard; fold
    back to `&[:Mem:]` when gcc's check is fixed.

    The class type is @a Mem's *declaring* class, so a flattened base's field
    reads through the base reference (exactly as a base pointer-to-member
    would).
    @tparam Mem a reflection of the (non-static) data member. */
template <std::meta::info Mem>
struct field_access {
    /** The declaring class. */
    using class_type = typename [:std::meta::parent_of(Mem):];
    /** The field's declared type (const included). */
    using field_type = typename [:std::meta::type_of(Mem):];

    /** Read the field (a reference, so backends can apply their
        reference-internal return semantics, matching the pointer-to-member
        path). @param c the object. @return the field. */
    static const field_type& get(const class_type& c) { return c.[:Mem:]; }
    /** Write the field (instantiated only for mutable fields).
        @param c the object. @param v the new value. */
    static void set(class_type& c, const field_type& v) { c.[:Mem:] = v; }
};

/** Whether an operator function is unary vs binary.

    Told apart by arity — this disambiguates the operators with both forms
    (`+`, `-`); backends use it to pick, e.g., `__neg__` vs `__sub__`. A
    non-static *member* operator's left operand is the implicit object (unary =
    0 parameters); a namespace-scope (or static member) operator spells both
    operands out (unary = 1 parameter).
    @param f a reflection of the operator function.
    @return `true` iff @a f is unary.
*/
consteval bool is_unary_operator(std::meta::info f) {
    const std::size_t n{std::meta::parameters_of(f).size()};
    return (std::meta::is_class_member(f) && !std::meta::is_static_member(f))
               ? n == 0
               : n == 1;
}

// --- aggregate initialization -----------------------------------------------
//
// An aggregate (a simple POD-like struct: no user-declared constructors) cannot
// be constructed as T(a, b) — only brace-initialized T{a, b}. A backend may want
// to synthesize a field constructor so the target language can build it with
// values; these helpers decide when that is valid and expose the fields.

/** The fields an aggregate is initialized from: its non-static data members in
    declaration order (all public, by the aggregate rules).

    @tparam T the aggregate type.
    @return an array of the field reflections, in declaration order.
*/
template <class T>
consteval auto aggregate_fields() {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    constexpr std::size_t n{std::meta::nonstatic_data_members_of(^^T, ctx).size()};
    std::array<std::meta::info, n> fs{};
    // Guard the fill: std::array<T, 0>::operator[] is not usable (its size-0
    // trap overload is not consteval, so merely instantiating it with the
    // consteval-only std::meta::info is an error for a fieldless type).
    if constexpr (n != 0) {
        std::size_t i{0};
        for (auto m : std::meta::nonstatic_data_members_of(^^T, ctx))
            fs[i++] = m;
    }
    return fs;
}

/** Whether to synthesize an aggregate field constructor for @a T (language @a L,
    resolution @a Resolution).

    Only for a baseless aggregate with at least one field, *all* of which
    participate: a based aggregate's brace-init nests the base (a flat field ctor
    can't express it), and a partially-excluded one would leak an excluded field
    as a positional parameter (aggregate init is positional and all-or-nothing).
    An empty aggregate is already covered by the default constructor.
    @tparam T          the aggregate type.
    @tparam L          the target language.
    @tparam Resolution the carriage's resolution (its `class_member_participates`
                       decides whether each field binds).
    @return `true` iff a field constructor should be synthesized.
*/
template <class T, lang L, class Resolution>
consteval bool aggregate_initializable() {
    if (!std::is_aggregate_v<T> || !welder::public_bases(^^T).empty())
        return false;
    constexpr auto ctx{std::meta::access_context::unchecked()};
    auto fields{std::meta::nonstatic_data_members_of(^^T, ctx)};
    if (fields.empty())
        return false;
    const policy_kind pol{policy_of(^^T)};
    for (auto m : fields) {
        // An UNNAMED field (an anonymous union, an unnamed bit-field) is
        // structurally unbindable — the member sweep skips it — so it counts
        // as non-participating here: synthesizing a field constructor would
        // leak it as a positional parameter (aggregate init is positional and
        // all-or-nothing).
        if (!std::meta::has_identifier(m))
            return false;
        if (!Resolution::class_member_participates(m, L, pol, ^^T))
            return false;
    }
    return true;
}

// --- namespace-member eligibility -------------------------------------------

/** The member kinds welder can expose from a namespace.

    `is_class_type`/`is_enum_type` throw on a non-type reflection, so they are
    reached only after `is_type`; the other predicates are total and safe on any
    reflection.
    @param mem a reflection of the namespace member.
    @return `true` iff @a mem is a class/enum type, a function, or a variable.
*/
consteval bool is_bindable_kind(std::meta::info mem) {
    return (std::meta::is_type(mem) &&
            (std::meta::is_class_type(mem) || std::meta::is_enum_type(mem))) ||
           std::meta::is_function(mem) || std::meta::is_variable(mem);
}

/** Whether a leaf entity binds: a welded candidate that also resolves as bound.

    @param mem a reflection of the namespace member.
    @param L   the target language.
    @param pol the namespace's policy.
    @return `true` iff @a mem is a bindable kind, welded for @a L, and bound under
            @a pol and its own marks.
*/
consteval bool entity_bound(std::meta::info mem, lang L, policy_kind pol) {
    return is_bindable_kind(mem) && welded_for(mem, L) && member_bound(mem, L, pol);
}

/** Whether @a ns holds anything that would bind, directly or nested.

    I.e. whether exposing it would yield a non-empty (sub)module. Each namespace
    contributes under its own policy; a nested namespace is recursed by the same
    rule as the dispatch (`member_bound` under the namespace's policy: automatic
    unless excluded, opt_in only if included).
    @param ns a reflection of the namespace.
    @param L  the target language.
    @return `true` iff some member (possibly nested) binds.
*/
consteval bool namespace_has_bound(std::meta::info ns, lang L) {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    const policy_kind pol{policy_of(ns)};
    for (auto mem : std::meta::members_of(ns, ctx)) {
        if (std::meta::is_namespace(mem)) {
            if (member_bound(mem, L, pol) && namespace_has_bound(mem, L))
                return true;
        } else if (entity_bound(mem, L, pol)) {
            return true;
        }
    }
    return false;
}

/** The **greedy** twin of namespace_has_bound: whether @a ns holds any *bindable
    kind* (ignoring the `weld` marker), directly or nested.

    Used by the tack-welding resolution to decide whether an unmarked namespace is
    worth turning into a submodule. Same shape as namespace_has_bound but with the
    `welded_for` gate dropped — a member counts if it is a bindable kind resolving as
    bound under the (marker-less) policy.
    @param ns a reflection of the namespace.
    @param L  the target language.
    @return `true` iff some member (possibly nested) is a bindable kind that binds.
*/
consteval bool namespace_has_bindable(std::meta::info ns, lang L) {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    const policy_kind pol{policy_of(ns)};
    for (auto mem : std::meta::members_of(ns, ctx)) {
        if (std::meta::is_namespace(mem)) {
            if (member_bound(mem, L, pol) && namespace_has_bindable(mem, L))
                return true;
        } else if (is_bindable_kind(mem) && member_bound(mem, L, pol)) {
            return true;
        }
    }
    return false;
}

// --- overload groups (resolution-aware) ---------------------------------------
//
// Several target frameworks store ONE entity per name — a Lua table key, a
// LuaCATS `function` declaration — so a name's C++ overloads must arrive as one
// group. The CARRIAGE owns that grouping (it visits a group's first participating
// overload, the *leader*, computes the whole set with these selectors and hands
// it to the rod's group hook): the rods never re-derive membership, so a group is
// exactly what the resolution admits — per-overload marks and bespoke
// (signature-level) resolutions stay consistent across every rod, and a mixed
// welded/unwelded set under one name can never split into clobbering halves.

/** The participating method overloads sharing @a fn's name and static-ness, from
    the class where @a fn is declared, in declaration order.
    @tparam Resolution the carriage's resolution.
    @param fn a reflection of one participating method (see is_method_candidate).
    @param L  the target language.
    @param bound_into the entity whose binding receives the group (the welded
           type — not necessarily `parent_of(fn)` when flattening a base).
    @return the overload set (always contains @a fn). */
template <class Resolution>
consteval std::vector<std::meta::info> method_overload_set(
    std::meta::info fn, lang L, std::meta::info bound_into) {
    const std::meta::info cls{std::meta::parent_of(fn)};
    const policy_kind pol{::welder::policy_of(cls)};
    const auto name{std::meta::identifier_of(fn)};
    const bool is_static{std::meta::is_static_member(fn)};
    std::vector<std::meta::info> out{};
    for (auto m : std::meta::members_of(cls, std::meta::access_context::unchecked()))
        if (is_method_candidate(m) &&
            member_access_admitted<Resolution>(m, L, bound_into) &&
            Resolution::class_member_participates(m, L, pol, bound_into) &&
            std::meta::is_static_member(m) == is_static &&
            std::meta::identifier_of(m) == name)
            out.push_back(m);
    return out;
}

/** Collect the participating *member* operators visible on @a bound_into's
    binding: @a src's own, preceded by those of its flattened (non-native
    according to @a Resolution) bases, recursively — mirroring `bind_members`'
    flattening, so a slot's group spans exactly what the class surface exposes.
    Each candidate passes access admission and the resolution under its
    *declaring* class's policy.
    @tparam Resolution the carriage's resolution.
    @param src        the class whose members to collect (a base during recursion).
    @param L          the target language.
    @param bound_into the welded type whose binding receives the operators.
    @param out        the collection target (bases append first). */
template <class Resolution>
consteval void collect_member_operators(std::meta::info src, lang L,
                                        std::meta::info bound_into,
                                        std::vector<std::meta::info>& out) {
    for (auto base : ::welder::public_bases(src))
        if (!Resolution::is_native_base(base, L, src))
            collect_member_operators<Resolution>(base, L, bound_into, out);
    const policy_kind pol{::welder::policy_of(src)};
    for (auto m :
         std::meta::members_of(src, std::meta::access_context::unchecked()))
        if (is_operator_candidate(m) &&
            member_access_admitted<Resolution>(m, L, bound_into) &&
            Resolution::class_member_participates(m, L, pol, bound_into))
            out.push_back(m);
}

/** Every participating operator entry of @a type's binding — member operators
    (own + flattened bases') and the **anchored free operators** of the type's
    enclosing namespace — in one list, so the carriage can slot-group across all
    sources (a one-value-per-slot backend must receive each slot whole). A free
    operator resolves like a member of its anchor: the type's policy plus the
    operator's own marks, through `class_member_participates` with the type as
    `bound_into`. The stringifier shape is excluded (see
    `stringifier_entries`); spaceship entries are included (the carriage routes
    them to comparison synthesis, never to a direct slot).
    @tparam Resolution the carriage's resolution.
    @param type the welded type.
    @param L    the target language.
    @return the entries, flattened bases first, then own members, then free. */
template <class Resolution>
consteval std::vector<std::meta::info> operator_entries(std::meta::info type,
                                                        lang L) {
    std::vector<std::meta::info> out{};
    collect_member_operators<Resolution>(type, L, type, out);
    const policy_kind pol{::welder::policy_of(type)};
    for (auto m : std::meta::members_of(enclosing_namespace(type),
                                        std::meta::access_context::unchecked()))
        if (free_operator_anchored(m, type) &&
            Resolution::class_member_participates(m, L, pol, type))
            out.push_back(m);
    return out;
}

/** The participating operator entries sharing @a fn's target slot (same
    operator and arity — hence the same special-method name) on @a bound_into's
    binding, across every source `operator_entries` sweeps (member + free).
    Matches the `overload_selector` shape so `overload_group` /
    `is_overload_leader` drive it like the other selectors.
    @tparam Resolution the carriage's resolution.
    @param fn a reflection of one participating entry.
    @param L  the target language.
    @param bound_into the welded type whose binding receives the group.
    @return the slot's group (always contains @a fn). */
template <class Resolution>
consteval std::vector<std::meta::info> operator_slot_set(
    std::meta::info fn, lang L, std::meta::info bound_into) {
    std::vector<std::meta::info> out{};
    for (auto m : operator_entries<Resolution>(bound_into, L))
        if (std::meta::operator_of(m) == std::meta::operator_of(fn) &&
            is_unary_operator(m) == is_unary_operator(fn))
            out.push_back(m);
    return out;
}

/** The participating stringifier entries for @a type: free
    `operator<<(std::ostream&, T)` overloads of its enclosing namespace, resolved
    like the anchored operators. At most one is bound (the target to-string
    protocols take no second operand — cv-variant duplicates would collide), so
    the carriage emits entry `[0]`.
    @tparam Resolution the carriage's resolution.
    @param type the welded type.
    @param L    the target language.
    @return the participating inserter reflections, in declaration order. */
template <class Resolution>
consteval std::vector<std::meta::info> stringifier_entries(std::meta::info type,
                                                           lang L) {
    std::vector<std::meta::info> out{};
    const policy_kind pol{::welder::policy_of(type)};
    for (auto m : std::meta::members_of(enclosing_namespace(type),
                                        std::meta::access_context::unchecked()))
        if (is_stringifier_for(m, type) &&
            Resolution::class_member_participates(m, L, pol, type))
            out.push_back(m);
    return out;
}

/** Which relational slots (`lt`, `le`, `gt`, `ge` — indexed by @ref cmp_slot)
    are already covered by an **explicit** participating operator on @a type's
    binding. Comparison synthesis from `operator<=>` fills only the uncovered
    slots: an explicit operator beats synthesis (mirroring C++ overload
    resolution's preference for non-rewritten candidates), and a slot is skipped
    whole so a one-value-per-slot backend never sees two writers.
    @tparam Resolution the carriage's resolution.
    @param type the welded type.
    @param L    the target language.
    @return covered flags, indexed by @ref cmp_slot. */
template <class Resolution>
consteval std::array<bool, 4> covered_comparison_slots(std::meta::info type,
                                                       lang L) {
    std::array<bool, 4> covered{};
    for (auto m : operator_entries<Resolution>(type, L)) {
        if (is_unary_operator(m))
            continue;
        switch (std::meta::operator_of(m)) {
            case std::meta::operators::op_less:
                covered[0] = true;
                break;
            case std::meta::operators::op_less_equals:
                covered[1] = true;
                break;
            case std::meta::operators::op_greater:
                covered[2] = true;
                break;
            case std::meta::operators::op_greater_equals:
                covered[3] = true;
                break;
            default:
                break;
        }
    }
    return covered;
}

/** The participating free-function overloads sharing @a fn's name, from @a fn's
    declaring namespace, in declaration order. Membership is the resolution's
    namespace-member verdict, so the set is exactly what the namespace walk binds.
    @tparam Resolution the carriage's resolution.
    @param fn a reflection of one participating namespace-scope function.
    @param L  the target language.
    @param bound_into the namespace being swept (== `parent_of(fn)`; passed for
           hook-signature uniformity).
    @return the overload set (always contains @a fn). */
template <class Resolution>
consteval std::vector<std::meta::info> function_overload_set(
    std::meta::info fn, lang L, std::meta::info bound_into) {
    const std::meta::info ns{std::meta::parent_of(fn)};
    const policy_kind pol{::welder::policy_of(ns)};
    const auto name{std::meta::identifier_of(fn)};
    std::vector<std::meta::info> out{};
    for (auto m : std::meta::members_of(ns, std::meta::access_context::unchecked()))
        if (std::meta::is_function(m) &&
            Resolution::member_participates(m, L, pol, bound_into) &&
            std::meta::identifier_of(m) == name)
            out.push_back(m);
    return out;
}

/** The signature of an overload-set selector specialization: the representative
    overload, the language, and the entity the group binds into. */
using overload_selector =
    std::vector<std::meta::info> (*)(std::meta::info, lang, std::meta::info);

/** @a Select's overload set for @a Fn as a fixed-size, splice-ready static array.
    @tparam Select    the selector specialization (e.g. `method_overload_set<R>`).
    @tparam Fn        the representative overload.
    @tparam L         the target language.
    @tparam BoundInto the entity whose binding receives the group.
    @return an array of the group's member reflections, in declaration order. */
template <overload_selector Select, std::meta::info Fn, lang L,
          std::meta::info BoundInto>
consteval auto overload_group() {
    constexpr std::size_t n{Select(Fn, L, BoundInto).size()};
    std::array<std::meta::info, n> out{};
    // Guard the fill: std::array<T, 0>::operator[] is not usable (n is >= 1 for a
    // group leader, but the guard keeps this well-formed regardless).
    if constexpr (n != 0) {
        auto v{Select(Fn, L, BoundInto)};
        for (std::size_t i{0}; i < n; ++i)
            out[i] = v[i];
    }
    return out;
}

/** Whether @a fn is the first (declaration order) member of its @a Select overload
    set — the single visit on which the carriage emits the whole group.
    @tparam Select the selector specialization.
    @param fn the candidate overload.
    @param L  the target language.
    @param bound_into the entity whose binding receives the group. */
template <overload_selector Select>
consteval bool is_overload_leader(std::meta::info fn, lang L,
                                  std::meta::info bound_into) {
    auto v{Select(fn, L, bound_into)};
    return !v.empty() && v.front() == fn;
}

/** The semi-manual (`weld_function<Fn>`) group: @a Fn first — the user's named
    entity resolves the group's target name — then @a Fn's participating siblings.

    Gathering the siblings keeps `weld_function` consistent with the namespace
    walk (and keeps one-value-per-name frameworks from silently clobbering): one
    call welds the name's participating overload set. @a Fn itself is included
    even when its own marks would resolve it out — the explicit call is the
    stronger statement of intent.
    An identifier-less @a Fn (a template instantiation formed with `substitute`)
    has no name for siblings to share, so its group is just itself.
    @tparam Resolution the carriage's resolution.
    @tparam Fn         the explicitly welded function.
    @tparam L          the target language.
    @return the group as a static array, @a Fn first. */
template <class Resolution, std::meta::info Fn, lang L>
consteval auto manual_function_group() {
    if constexpr (!std::meta::has_identifier(Fn)) {
        return std::array<std::meta::info, 1>{Fn};
    } else {
        constexpr std::size_t n{[] {
            auto v{function_overload_set<Resolution>(Fn, L,
                                                     std::meta::parent_of(Fn))};
            std::size_t extra{1};
            for (auto m : v)
                if (m == Fn)
                    extra = 0;
            return v.size() + extra;
        }()};
        std::array<std::meta::info, n> out{};
        out[0] = Fn;
        std::size_t i{1};
        for (auto m :
             function_overload_set<Resolution>(Fn, L, std::meta::parent_of(Fn)))
            if (m != Fn)
                out[i++] = m;
        return out;
    }
}

/** The participating constructors of @a Type under policy @a Pol: every
    bindable-shape constructor (see is_bindable_constructor) the resolution
    admits, in declaration order.

    Constructors resolve **symmetrically** with every other member — the type's
    policy and the constructor's own marks decide, per constructor (`opt_in`
    binds only marked-`include` constructors). The carriage guards the silent
    consequence: filtering that leaves a type with *no* constructor at all is a
    hard error unless the emptiness is explicit (see bind_type's
    no-constructor-left static_assert), which is why @a Pol is a parameter — the
    guard probes the same resolution under `policy_kind::automatic` as its
    baseline.

    The default constructor is decided separately (it may be implicit, hence not
    a member — see @ref default_ctor_admitted), as is the synthesized aggregate
    constructor.
    @tparam Resolution the carriage's resolution.
    @tparam Type       the class type reflection.
    @tparam L          the target language.
    @tparam Pol        the policy to resolve under (the type's own, or
                       `automatic` for the guard's baseline).
    @return the constructor reflections as a static array. */
template <class Resolution, std::meta::info Type, lang L, policy_kind Pol>
consteval auto ctor_group() {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    constexpr std::size_t n{[] {
        std::size_t count{0};
        for (auto c : std::meta::members_of(Type, ctx))
            if (is_bindable_constructor(c) &&
                Resolution::class_member_participates(c, L, Pol, Type))
                ++count;
        return count;
    }()};
    std::array<std::meta::info, n> out{};
    if constexpr (n != 0) {
        std::size_t i{0};
        for (auto c : std::meta::members_of(Type, ctx))
            if (is_bindable_constructor(c) &&
                Resolution::class_member_participates(c, L, Pol, Type))
                out[i++] = c;
    }
    return out;
}

/** Whether the resolution admits @a Type's DEFAULT constructor.

    The default constructor may be *implicit* — no declaration, so no marks and
    nothing for `opt_in` to filter: it is admitted whenever the type is
    default-constructible (the carriage checks constructibility itself, against
    the rod's construction type). A *declared* default constructor is consulted
    under `policy_kind::automatic` — its explicit marks are honored (so
    `[[=welder::mark::exclude]] T() = default;` suppresses default construction),
    but `opt_in`'s default-out is not: you cannot `include` an implicit
    constructor, so filtering the declared spelling by default would make
    `T() = default;` (a C++ no-op) silently change the binding.
    @tparam Resolution the carriage's resolution.
    @tparam Type       the class type reflection.
    @tparam L          the target language. */
template <class Resolution, std::meta::info Type, lang L>
consteval bool default_ctor_admitted() {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    for (auto c : std::meta::members_of(Type, ctx))
        if (std::meta::is_constructor(c) && std::meta::parameters_of(c).empty())
            return std::meta::is_public(c) && !std::meta::is_deleted(c) &&
                   Resolution::class_member_participates(
                       c, L, policy_kind::automatic, Type);
    return true; // implicit (or absent): constructibility alone decides
}

/** Whether the resolution admits @a Type's **copy** constructor — the marks half
    of the copy decision; the carriage pairs it with
    `std::is_copy_constructible_v<T>` (which also rules out abstract,
    deleted-copy and inaccessible-copy types) and hands the result to the rod's
    `add_constructors` as its `Copyable` flag. A copy constructor never binds as
    an init overload (see @ref is_bindable_constructor); its target-language
    spelling is the rod's — the Python rods emit `__copy__`/`__deepcopy__` over
    it, the Lua rods ignore it (Lua has no copy protocol).

    Mirrors @ref default_ctor_admitted exactly: the copy constructor may be
    *implicit* — no declaration, so no marks and nothing for `opt_in` to filter:
    admitted whenever the type is copy-constructible. A *declared* copy
    constructor is consulted under `policy_kind::automatic` — its explicit marks
    are honored (so `[[=welder::mark::exclude]] T(const T&);` suppresses the
    copy binding, per language when the mark is scoped), but `opt_in`'s
    default-out is not: you cannot `include` an implicit constructor, so
    filtering the declared spelling by default would make
    `T(const T&) = default;` (a C++ no-op) silently change the binding.
    @tparam Resolution the carriage's resolution.
    @tparam Type       the class type reflection.
    @tparam L          the target language. */
template <class Resolution, std::meta::info Type, lang L>
consteval bool copy_ctor_admitted() {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    bool declared{false};
    for (auto c : std::meta::members_of(Type, ctx))
        if (std::meta::is_copy_constructor(c)) {
            declared = true;
            if (std::meta::is_public(c) && !std::meta::is_deleted(c) &&
                Resolution::class_member_participates(
                    c, L, policy_kind::automatic, Type))
                return true;
        }
    return !declared; // implicit: constructibility alone decides
}

/** Reject an `include`/`only` mark on a **move constructor** of @a Type.

    Move construction never crosses a language boundary — no target language
    has move semantics — so a move constructor is skipped structurally
    everywhere (it is not a bindable-shape constructor, and the copy decision
    never consults it). An `include` or `only` mark on one is therefore an
    intent welder cannot honor: diagnosed as a hard error rather than silently
    dropped. `mark::exclude` stays a harmless no-op (excluding what never binds
    restricts nothing).
    @tparam Type the class type reflection.
    @throws diag::marked_move_constructor (a constant-evaluation error) on a
            marked move constructor. */
template <std::meta::info Type>
consteval void validate_move_ctor_marks() {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    for (auto c : std::meta::members_of(Type, ctx))
        if (std::meta::is_move_constructor(c) &&
            (!std::meta::annotations_of_with_type(c, ^^include_spec).empty() ||
             !std::meta::annotations_of_with_type(c, ^^only_spec).empty()))
            throw diag::marked_move_constructor{};
}

// --- inheritance: native bases ----------------------------------------------
//
// `weld` marks a type as an independently-registered, module-discoverable entity,
// NOT as an inheritance directive. The most-derived type's `weld` drives which
// languages bind; a base need not be welded to contribute its members. Two kinds
// of public base fall out for a binding:
//   * a welded base -> registered as its own target-language class, linked via
//     native inheritance (passed as a base of the derived class handle).
//   * a non-welded base -> a plain C++ mixin with no standalone type, whose
//     eligible members are flattened onto the derived binding.

/** Collect the native bases of @a type for @a L: its nearest welded ancestors.

    Found by looking *past* non-welded bases (whose members are flattened instead),
    so a welded base reachable only through a non-welded one is still linked. A
    virtual diamond can reach the same welded base by several paths, so the list is
    deduplicated.
    @param type a reflection of the derived type.
    @param L    the target language.
    @param[out] out accumulates the deduplicated native base reflections.
*/
consteval void collect_native_bases(std::meta::info type, lang L,
                                    std::vector<std::meta::info>& out) {
    for (auto base : welder::public_bases(type)) {
        if (welder::welded_for(base, L)) {
            bool seen{false};
            for (auto e : out)
                if (e == base) {
                    seen = true;
                    break;
                }
            if (!seen)
                out.push_back(base);
        } else {
            collect_native_bases(base, L, out); // descend through the mixin
        }
    }
}

/** The native bases of @a Type for @a L as a static array of type reflections.

    Usable as a non-type template argument (spliced into the backend's class handle
    template). Same set as collect_native_bases().
    @tparam Type the derived type reflection.
    @tparam L    the target language.
    @return an array of the native base type reflections.
*/
template <std::meta::info Type, lang L>
consteval auto native_base_types() {
    constexpr std::size_t n{[] {
        std::vector<std::meta::info> v;
        collect_native_bases(Type, L, v);
        return v.size();
    }()};
    std::array<std::meta::info, n> types{};
    // Guard the fill: std::array<T, 0>::operator[] is not consteval, so it must
    // not be instantiated when a type has no native bases (the common case).
    if constexpr (n != 0) {
        std::vector<std::meta::info> v;
        collect_native_bases(Type, L, v);
        std::size_t i{0};
        for (auto base : v)
            types[i++] = base;
    }
    return types;
}

} // namespace welder::detail
