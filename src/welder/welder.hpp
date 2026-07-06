#pragma once
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
    lays down the bindings): a stateless policy type (`welder::rods::pybind11::rod`,
    `…::nanobind::rod`, `…::sol2::rod`) supplying a fixed set of emission
    primitives. The driver here is written once against the @ref welder::rod concept
    and reused verbatim by every rod, so a new rod implements only its primitives,
    never the traversal/resolution logic.

    The public face of all of it is `welder::welder<Rod>` at the bottom of this
    file: one struct, parameterized on the rod, whose static members run the
    reflection-driven binding at whichever stage of the usual hand-binding flow you
    want to automate (a single type, a namespace into an existing module, a
    namespace as a fresh submodule, or a whole module).

    Provide the vocabulary first — `import welder;` or
    `#include <welder/vocabulary.hpp>` — then this header (each backend header
    includes it for you).
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
    template <std::meta::info Fn,  class Style> static void add_function(module_type&);
    template <std::meta::info Var, class Style> static void add_variable(module_type&, session&);
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

// bind_type and bind_namespace_driver are mutually recursive: a namespace member
// that is a class type binds via bind_type, and a nested namespace recurses. Each
// carries the name-style @a Style (see <welder/naming.hpp>) so every generated name
// flows through it (the default naming::none binds C++ identifiers unchanged).
template <rod B, class T, class Style = naming::none>
auto bind_type(typename B::module_type& m, const char* name);
template <rod B, class E, class Style = naming::none>
auto bind_enum(typename B::module_type& m, const char* name);
template <rod B, std::meta::info Ns, class Style = naming::none>
void bind_namespace_driver(typename B::module_type& m);

/** Reflect over enum @a E and register it via rod @a B onto module @a m.

    Emits its docstring, then each enumerator that resolves as bound (an enumerator
    honors the enum's policy and its own exclude/include marks, exactly like a data
    member). `finish_enum` lets the backend apply a whole-enum finalizer — e.g.
    exporting an unscoped enum's values into the enclosing scope, mirroring C++.

    @tparam B the rod.
    @tparam E the enum type.
    @param m    the module handle to register onto.
    @param name the target name, or `nullptr` to default to @a E's identifier.
    @return the rod's enum handle.
*/
template <rod B, class E, class Style>
auto bind_enum(typename B::module_type& m, const char* name) {
    constexpr lang L{B::language};
    static_assert(welder::welded_for(^^E, L),
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

/** Flatten the eligible public data members, methods and operators of @a Src onto
    the class handle @a cls (a handle for a type deriving from @a Src).

    Non-welded (mixin) bases are recursed *first* so that, on a name clash, the
    member declared closer to the derived type wins. Constructors are never
    flattened (the derived type provides its own). A welded base is skipped here —
    it binds natively, as a base of the class handle (see `native_base_types`).

    @tparam B     the rod.
    @tparam Src   a reflection of the (base or derived) type whose members to flatten.
    @tparam Style the name style each member's name flows through.
    @tparam Cls   the rod's class-handle type (deduced).
    @param cls the class handle to register onto.
*/
template <rod B, std::meta::info Src, class Style, class Cls>
void bind_members(Cls& cls) {
    constexpr lang L{B::language};
    constexpr auto ctx{std::meta::access_context::unchecked()};

    template for (constexpr auto base :
                  std::define_static_array(welder::public_bases(Src))) {
        if constexpr (!welder::welded_for(base, L))
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

/** Reflect over @a T and register it via rod @a B onto module @a m.

    Emits:
    - native inheritance from @a T's nearest welded ancestors (each a base of the
      class handle; bind those bases separately, before @a T);
    - the default constructor (if any), each public non-copy/move constructor, and
      — for a baseless aggregate whose fields all bind — a synthesized field
      constructor;
    - public data members / methods / operators that resolve as bound, plus the
      eligible members of every non-welded public base, flattened in.

    @tparam B the rod.
    @tparam T the type to bind.
    @param m    the module handle to register onto.
    @param name the target name, or `nullptr` to default to @a T's identifier.
    @return the rod's class handle, so callers can chain further registrations.
*/
template <rod B, class T, class Style>
auto bind_type(typename B::module_type& m, const char* name) {
    constexpr lang L{B::language};
    static_assert(welder::welded_for(^^T, L),
                  "welder: weld_type<T>: T is not welded for this backend's language; "
                  "annotate it with [[=welder::weld(...)]]");
    constexpr auto ctx{std::meta::access_context::unchecked()};

    const char* cls_name{
        name ? name : welder::name_of<^^T, L, Style, ent_kind::class_>()};

    // Native (welded) bases → bases of the class handle; the user binds them first.
    constexpr auto bases{native_base_types<^^T, L>()};
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

    // Data members + methods + operators (T's own, plus flattened non-welded bases).
    bind_members<B, ^^T, Style>(cls);

    return cls;
}

/** Reflect over a whole namespace @a Ns and expose its members on module @a m.

    `weld` makes an entity a candidate; the namespace `policy` (default automatic)
    and per-member marks then resolve what binds. Classes bind via bind_type(), free
    functions and namespace variables become module attributes, and a nested
    namespace holding bound content becomes a submodule (recursed under its own
    policy). Members are visited in declaration order.

    @tparam B  the rod.
    @tparam Ns a reflection of the namespace.
    @param m the module handle to fill.
*/
template <rod B, std::meta::info Ns, class Style>
void bind_namespace_driver(typename B::module_type& m) {
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
            if constexpr (welder::welded_for(mem, L) && member_bound(mem, L, pol))
                bind_type<B, typename [:mem:], Style>(m, nullptr);
        } else if constexpr (std::meta::is_type(mem) && std::meta::is_enum_type(mem)) {
            if constexpr (welder::welded_for(mem, L) && member_bound(mem, L, pol))
                bind_enum<B, typename [:mem:], Style>(m, nullptr);
        } else if constexpr (std::meta::is_function(mem)) {
            if constexpr (welder::welded_for(mem, L) && member_bound(mem, L, pol)) {
                welder::assert_callable_bindable<B, mem, L>();
                B::template add_function<mem, Style>(m);
            }
        } else if constexpr (std::meta::is_variable(mem)) {
            if constexpr (welder::welded_for(mem, L) && member_bound(mem, L, pol)) {
                welder::assert_member_bindable<B, mem, L>();
                B::template add_variable<mem, Style>(m, session);
            }
        } else if constexpr (std::meta::is_namespace(mem)) {
            // A nested namespace resolves like a leaf under the parent policy (but
            // is never welded): automatic recurses unless excluded, opt_in only if
            // included. It becomes a submodule when it holds bound content (then
            // resolved under its *own* policy).
            if constexpr (member_bound(mem, L, pol) && namespace_has_bound(mem, L)) {
                auto sub{B::add_submodule(
                    m, welder::name_of<mem, L, Style, ent_kind::submodule>())};
                bind_namespace_driver<B, mem, Style>(sub);
            }
        }
    }

    B::close_module(m, session);
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
template <rod B, std::meta::info Ns, class Style, class Pre, class Post>
void build_module_driver(typename B::module_type& m, Pre pre, Post post) {
    static_assert(std::meta::is_namespace(Ns),
                  "welder: weld_module<Ns>: Ns must reflect a namespace");
    static_assert(std::meta::parent_of(Ns) == ^^::,
                  "welder: weld_module<Ns>: Ns must be a top-level namespace (its "
                  "name is meant to be the module name)");
    pre(m);
    bind_namespace_driver<B, Ns, Style>(m);
    post(m);
}

} // namespace detail

/** welder's binding entry point, parameterized on a **rod**.

    One struct is all a user drives: pick a rod (e.g. `welder::rods::pybind11::rod`,
    from that rod's header) and call the static member matching the stage of the
    usual hand-binding flow you want to automate — welder generates the
    backend-agnostic boilerplate there, and everything around it stays ordinary
    hand-written binding code.

    @code
    #include <welder/rods/python/pybind11/rod.hpp>
    using weld = welder::welder<welder::rods::pybind11::rod>;

    PYBIND11_MODULE(mymod, m) {
        weld::weld_type<MyType>(m);          // one type onto an existing module
        weld::weld_namespace<^^myns>(m);     // a namespace's members into m
        weld::weld_namespace_as_submodule<^^other>(m); // …or into m.other
    }
    @endcode

    (For zero hand-written entry-point code at all, each rod also ships a
    `module.hpp` with the `WELDER_MODULE` entry-point macro.)

    @tparam B     the rod to emit through (any type satisfying @ref welder::rod).
    @tparam Style the name style every generated name flows through — a class,
                  method, field, … is renamed into the target language's convention
                  (see `<welder/naming.hpp>`). A `[[=welder::weld_as]]` override on an
                  entity beats the style and is used verbatim. Defaults to
                  @ref welder::naming::none (bind C++ identifiers unchanged); a Python
                  binding might pass `welder::rods::python::pep8`.
*/
template <rod B, class Style = naming::none>
struct welder {
    /** The rod this instantiation emits through. */
    using rod_type = B;
    /** The name style this instantiation renames through. */
    using name_style = Style;
    /** The rod's module handle (`py::module_`, `sol::table`, …). */
    using module_type = typename B::module_type;

    /** A do-nothing module hook; the default for weld_module()'s pre/post. */
    static constexpr auto noop{[](module_type&) {}};

    /** Reflect over @a T and register it on module @a m.

        A class type binds with its constructors, fields, methods, operators and
        welded bases; an enum with its enumerators (see the driver above for the
        full set). @a T must be welded for `B::language`.

        @tparam T the class or enum type to bind.
        @param m    the module handle to register onto.
        @param name the target-language name, or `nullptr` to default to @a T's
                    identifier.
        @return the rod's class/enum handle, for chaining further
                registrations. */
    template <class T>
    static auto weld_type(module_type& m, const char* name = nullptr) {
        if constexpr (std::is_enum_v<T>)
            return detail::bind_enum<B, T, Style>(m, name);
        else
            return detail::bind_type<B, T, Style>(m, name);
    }

    /** Reflect over namespace @a Ns and expose its welded members on module @a m.

        Classes and enums bind via weld_type(), free functions and namespace
        variables become module attributes, and a nested namespace holding bound
        content becomes a submodule.

        @tparam Ns a reflection of the namespace.
        @param m the module handle to fill.
        @return @a m, for chaining. */
    template <std::meta::info Ns>
    static module_type& weld_namespace(module_type& m) {
        detail::bind_namespace_driver<B, Ns, Style>(m);
        return m;
    }

    /** Define a submodule of @a m (via the backend) and weld_namespace() @a Ns
        into it.

        @tparam Ns a reflection of the namespace.
        @param m    the parent module handle.
        @param name the submodule name, or `nullptr` to default to @a Ns's
                    identifier.
        @return the new submodule handle, for chaining. */
    template <std::meta::info Ns>
    static module_type weld_namespace_as_submodule(module_type& m,
                                                   const char* name = nullptr) {
        static_assert(std::meta::is_namespace(Ns),
                      "welder: weld_namespace_as_submodule<Ns>: Ns must reflect a "
                      "namespace");
        module_type sub{B::add_submodule(
            m, name ? name
                    : ::welder::name_of<Ns, B::language, Style,
                                        ::welder::ent_kind::submodule>())};
        detail::bind_namespace_driver<B, Ns, Style>(sub);
        return sub;
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
        detail::build_module_driver<B, Ns, Style>(m, pre, post);
        return m;
    }
};

} // namespace welder
