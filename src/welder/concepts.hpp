#pragma once
#include <concepts>
#include <meta>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>

/** @file
    The **core interface concepts** welder's static polymorphism rests on, gathered
    in one catalogue: the customization-point contracts a rod (or a user) plugs into.

    Five concepts live here, each the compile-time *shape guard* for one seam:

    - @ref welder::rod — the emission contract a **rod** (a welding rod: the backend
      that lays a framework's bindings down) satisfies to plug into the carriage.
    - @ref welder::caster_oracle — the one bindability fact a rod must expose: can it
      represent a type *natively*, without welder registering a class/enum for it?
    - @ref welder::resolution — the *which-participates* contract a **resolution**
      (the carriage's other injected policy) satisfies: it reads welder's markers to
      decide what the carriage binds, kept apart from the rod's *how*.
    - @ref welder::naming::name_style — the per-kind name-styling hooks a naming style
      supplies (`transform_class`, `transform_method`, …).
    - @ref welder::doc_style — how a documentation style folds a @ref
      welder::detail::function_doc into one docstring.

    The concepts are pooled here — rather than each sitting inside the header of the
    machinery it constrains — so the interface surface reads as one place; the
    machinery itself stays in its own header (`bindable.hpp`, `naming.hpp`, `doc.hpp`,
    the carriage), each of which includes this one.

    @note Like the rest of the reflection layer (`reflect.hpp`, `naming.hpp`, …), this
    *names* the welder vocabulary (`lang`) but deliberately does NOT include
    `<welder/annotations.hpp>`/`<welder/lang.hpp>`. Provide the vocabulary first —
    `#include <welder/vocabulary.hpp>` — then any header that pulls this in.
*/

namespace welder::inline v0 {

namespace detail {

/** A placeholder class type for *shape*-probing a member template inside a concept.

    A concept cannot quantify over every type a member template accepts, so the seam
    concepts here (@ref welder::caster_oracle, @ref welder::resolution) instantiate the
    hook once with this stand-in and check only the resulting shape — the value it
    answers is never inspected. It is a complete, base-less class (so a probe that
    reflects over its bases sees an empty set) that carries no meaning of its own: it
    simply spells "the type is irrelevant here". */
struct any_type {};

/** A placeholder *enum* type for shape-probing the enum hooks against a rod's
    `enum_handle_type<E>` — @ref any_type is a class, so it cannot stand in for an
    enum. Empty on purpose; the concept never inspects its enumerators. */
enum class any_enum {};

} // namespace detail

/** The raw documentation pieces @ref doc_style assembles; its full definition (a data
    struct) lives in `<welder/doc.hpp>`. Forward-declared here so this concepts header
    stays a dependency-light leaf the machinery headers can include. */
namespace detail {
struct function_doc;
} // namespace detail

/** The one bindability fact a backend must provide: can it natively convert a
    type *without* welder registering it?

    `has_native_caster<T>` is `true` for a native scalar/string/STL type, or one
    the user gave a bespoke backend converter; `false` means @a T is a
    program-defined class/enum the backend can only handle once welder has
    registered it (so welder then requires @a T to be welded).

    This concept is a *shape* guard, not a per-type correctness guarantee: it only
    checks that @a B exposes the `has_native_caster` member template and that it
    yields something bool-convertible, so a backend that forgot it fails here with a
    clear "not a `caster_oracle`" instead of a deep error inside bindable(). A
    concept cannot quantify over every @a T, so we probe with a single arbitrary
    type — @ref welder::detail::any_type. Any complete type would do; the value it
    answers is never inspected.

    @tparam B the rod type.
*/
template <class B>
concept caster_oracle = requires {
    { B::template has_native_caster<detail::any_type> } -> std::convertible_to<bool>;
};

/** The contract a **rod** (a welder backend, `welder::rods::…::rod`) must satisfy
    to plug into the generic driver.

    A rod `B` is a stateless struct; nothing is inherited, and every member is
    static. The concept *shape*-checks almost the whole contract — the associated types,
    the module/session machinery, and every emission hook — so a rod that omits or
    mis-signs one fails here with a clear "not a `welder::rod`" rather than a deep error
    the first time the driver instantiates it. The templated hooks can't be quantified
    over every type/reflection, so — as @ref welder::caster_oracle probes
    `has_native_caster` — they are probed once with fixed placeholders (@ref
    welder::detail::any_type, @ref welder::detail::any_enum, `^^int`), checking *shape*
    (the hook exists with a compatible signature), not per-type correctness. The
    per-class / per-enum hooks are probed against the rod's own declared handle types
    (`class_handle_type` / `enum_handle_type`); every emission hook returns void, so only
    signatures are substituted and no hook body is instantiated.

    @note **The one gap: the factories themselves.** `make_class` and `make_enum`
    return `auto`, so merely naming them in a requirement would force their bodies to be
    instantiated to deduce the return type — and a rod's `make_class` legitimately does
    real work there (the sol2 rod registers a type's constructors inside it), which a
    placeholder type does not survive. So the two factories stay
    **contract-by-documentation**, enforced when the driver instantiates the rod over a
    real welded type. The concept sidesteps them by requiring the handle types as
    *associated aliases* (`class_handle_type<T>` / `enum_handle_type<E>` — what the
    factories yield) rather than deducing them from a factory call, which is what lets
    the per-handle hooks be checked without instantiating any factory body.

    **Associated:**
    @code
    static constexpr lang language;   // the target language B binds to
    using  module_type = ...;         // B's module handle (passed by ref)
    template <class T> static constexpr bool has_native_caster;  // caster_oracle
    template <class T> using class_handle_type = ...;  // what make_class yields for T
    template <class E> using enum_handle_type  = ...;  // what make_enum  yields for E
    @endcode

    **Type binding** (the class handle is `class_handle_type<T>`, what `make_class`
    returns). The per-member hooks take a trailing `class Style` (a
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
    // Module / session machinery + the handle-free per-entity hooks. Every requirement
    // here takes concrete arguments and returns void or an explicit type, so only the
    // signatures are substituted — no hook body is instantiated (the reflection/type
    // template arguments, e.g. `^^int` / `detail::any_type`, reach only the bodies,
    // which a requires-expression leaves untouched). The per-handle hooks follow in
    // their own conjunct below; only the make_class/make_enum factories stay unprobed
    // (see the concept's @note).
    requires(typename B::module_type& m, const char* s,
             std::remove_cvref_t<decltype(B::open_module(
                 std::declval<typename B::module_type&>()))> session) {
        { B::open_module(m) };
        { B::add_submodule(m, s) } -> std::same_as<typename B::module_type>;
        B::set_module_doc(m, s);
        B::close_module(m, session);
        { B::special_method_name(^^int) } -> std::convertible_to<const char*>;
        B::template add_function<^^int, detail::any_type>(m);
        B::template add_variable<^^int, detail::any_type>(m, session);
    } &&
    // The per-class / per-enum hooks, probed against the rod's declared handle types
    // (@ref class_handle_type / @ref enum_handle_type — the associated types naming what
    // make_class / make_enum yield). All return void, so given the handle type only the
    // signatures are checked; the placeholder reflections (`^^int`) / types
    // (@ref detail::any_type) reach only the bodies, which are not instantiated.
    requires(typename B::template class_handle_type<detail::any_type>& cls,
             typename B::template enum_handle_type<detail::any_enum>& en) {
        B::add_default_ctor(cls);
        B::template add_constructor<^^int>(cls);
        B::template add_aggregate_constructor<detail::any_type>(cls);
        B::template add_field<^^int, detail::any_type>(cls);
        B::template add_method<^^int, detail::any_type>(cls);
        B::template add_static_method<^^int, detail::any_type>(cls);
        B::template add_operator<^^int>(cls);
        B::template add_enumerator<^^int, detail::any_type>(en);
        B::template finish_enum<detail::any_enum>(en);
    };

/** The contract a **resolution** — the carriage's *which-participates* policy — must
    satisfy to be injected as `welder::carriages::basic_carriage`'s `Resolution` argument.

    A carriage separates three concerns: *which* entities participate (the reading of
    welder's markers — the resolution's job), *how* they are emitted (the @ref welder::rod),
    and *whether* they are representable (the bindability gate, shared). A resolution
    `R` is a stateless struct of static `consteval` predicates the carriage consults as
    it walks a reflected type or namespace. Two ship — `marker_resolution` (honor
    `weld`/`policy`/marks — the default `welder::stitch_welding_carriage`) and
    `greedy_resolution` (ignore the markers, bind an unmarked library greedily —
    `welder::tack_welding_carriage`) — and a user may inject a bespoke one.

    **Predicates** (each `static consteval`, mirroring the carriage's call sites):
    @code
    static consteval bool participates(std::meta::info entity, lang L);
        // a leaf type / function / variable participates for language L
    static consteval bool is_native_base(std::meta::info base, lang L);
        // base binds separately (as a base of the class handle) vs. being flattened in
    static consteval bool member_participates(std::meta::info mem, lang L, policy_kind pol);
        // a namespace member participates under its enclosing scope's policy
    static consteval bool namespace_participates(std::meta::info ns, lang L, policy_kind pol);
        // a nested namespace becomes a (recursed) submodule
    @endcode

    Plus one reflection-templated hook — it takes the derived type and a `lang` as
    non-type template arguments:
    @code
    template <std::meta::info T, lang L> static consteval auto native_bases();
        // T's native-base reflections (a std::array<std::meta::info, N>), spliced into
        // the class handle by make_class
    @endcode
    A concept cannot quantify over every `T`, so — exactly as @ref welder::caster_oracle probes
    `has_native_caster` — this probes `native_bases` with the single placeholder
    @ref welder::detail::any_type and checks only its *shape* (that the hook exists and
    yields a range of `std::meta::info`). The actual per-type instantiation is still
    exercised at the carriage's call site.

    @tparam R the candidate resolution type.
*/
template <class R>
concept resolution =
    requires(std::meta::info e, lang L, policy_kind pol) {
        { R::participates(e, L) } -> std::convertible_to<bool>;
        { R::is_native_base(e, L) } -> std::convertible_to<bool>;
        { R::member_participates(e, L, pol) } -> std::convertible_to<bool>;
        { R::namespace_participates(e, L, pol) } -> std::convertible_to<bool>;
    } &&
    requires {
        { R::template native_bases<^^detail::any_type, lang{}>() }
            -> std::ranges::range;
        requires std::same_as<
            std::ranges::range_value_t<
                decltype(R::template native_bases<^^detail::any_type, lang{}>())>,
            std::meta::info>;
    };

/** A *style* folds a @ref welder::detail::function_doc into one docstring.

    It is the customization point for how documentation reads in the target
    language; swap it to emit Google-, NumPy-, or any house style. Any type with
    `static std::string format(const detail::function_doc&)` qualifies. Concrete styles
    live with the rods that share them (e.g. `welder::rods::python::google_style`
    in `<welder/rods/python/doc_style.hpp>`), keeping this core layer neutral.

    @tparam S the candidate style type.
*/
template <class S>
concept doc_style = requires(const detail::function_doc& d) {
    { S::format(d) } -> std::same_as<std::string>;
};

namespace naming {

/** A **name style** names every kind of entity welder can bind, through one hook
    per kind. Each hook takes the entity's reflection and returns its target name.
    The driver calls the hook matching what it is binding — so a style varies naming
    by kind without inspecting the reflection itself.

    A style implements all of `transform_class`, `transform_enum`,
    `transform_enumerator`, `transform_method`, `transform_static_method`,
    `transform_function`, `transform_field`, `transform_variable` and
    `transform_submodule` as `static consteval std::string(std::meta::info)`. In
    practice a style *inherits* @ref welder::naming::none (or a single-convention style) and
    overrides only the hooks that differ — static-hook hiding does the rest, since
    welder always calls through the concrete style type.
    @tparam S the candidate style type. */
template <class S>
concept name_style = requires {
    { S::transform_class(^^int) } -> std::convertible_to<std::string>;
    { S::transform_enum(^^int) } -> std::convertible_to<std::string>;
    { S::transform_enumerator(^^int) } -> std::convertible_to<std::string>;
    { S::transform_method(^^int) } -> std::convertible_to<std::string>;
    { S::transform_static_method(^^int) } -> std::convertible_to<std::string>;
    { S::transform_function(^^int) } -> std::convertible_to<std::string>;
    { S::transform_field(^^int) } -> std::convertible_to<std::string>;
    { S::transform_variable(^^int) } -> std::convertible_to<std::string>;
    { S::transform_submodule(^^int) } -> std::convertible_to<std::string>;
};

} // namespace naming

} // namespace welder