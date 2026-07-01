#pragma once
#include <concepts>
#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

#include <welder/bind_traits.hpp> // what-binds selection layer
#include <welder/bindable.hpp>    // bindability gate + caster_oracle
#include <welder/doc.hpp>         // doc_of (class / namespace docstrings)
#include <welder/reflect.hpp>     // welded_for / policy_of / member_bound

// The backend interface + the generic binding driver.
//
// welder's core walks a reflected type/namespace and decides *what* to bind
// (bind_traits.hpp) and *whether each type can be represented* (bindable.hpp).
// Everything language-specific — *how* to register a class, method, property or
// module attribute — is delegated to a **backend**: a stateless policy type
// (pybind11 today; nanobind / lua later) supplying a fixed set of emission
// primitives. The driver below is written once against the `backend` concept and
// reused verbatim by every backend, so a new backend implements only its
// primitives, never the traversal/resolution logic.
//
// ── The backend contract ────────────────────────────────────────────────────
// A backend `B` is a struct providing (nothing is inherited; all are static):
//
//   Associated:
//     static constexpr lang language;   // the target language B binds to
//     using  module_type = ...;         // B's module handle (passed by ref)
//     template <class T> static constexpr bool has_native_caster;  // caster_oracle
//
//   Type binding (the class handle is whatever make_class returns; deduced):
//     template <class T, auto Bases, std::size_t... I>
//       static auto make_class(module_type&, const char* name, const char* doc,
//                              std::index_sequence<I...>);   // Bases[I] spliced
//     static void add_default_ctor(auto& cls);
//     template <std::meta::info Ctor> static void add_constructor(auto& cls);
//     template <class T>              static void add_aggregate_constructor(auto& cls);
//     template <std::meta::info Mem>  static void add_field(auto& cls);
//     template <std::meta::info Fn>   static void add_method(auto& cls);
//     template <std::meta::info Fn>   static void add_static_method(auto& cls);
//     template <std::meta::info Fn>   static void add_operator(auto& cls);
//     static consteval const char* special_method_name(std::meta::info op_fn);
//         // target special-method name for a member operator, or nullptr if the
//         // backend does not expose it (drives add_operator eligibility)
//
//   Enum binding (the enum handle is whatever make_enum returns; deduced):
//     template <class E> static auto make_enum(module_type&, const char* name,
//                                              const char* doc);
//     template <std::meta::info Enum> static void add_enumerator(auto& e);
//     template <class E> static void finish_enum(auto& e); // e.g. export unscoped
//
//   Namespace / module binding (a "session" is backend scratch state — e.g. an
//   accumulator for deferred, batched attributes — obtained per (sub)module):
//     static auto open_module(module_type&);              // -> session
//     static void set_module_doc(module_type&, const char* doc);
//     template <std::meta::info Fn>  static void add_function(module_type&);
//     template <std::meta::info Var> static void add_variable(module_type&, session&);
//     static module_type add_submodule(module_type&, const char* name);
//     static void close_module(module_type&, session&);   // finalize the session
//
// The concept below checks the statically-checkable surface (associated types,
// the module machinery); the class-level and per-member hooks are templated on a
// reflection/type, so they are contract-by-documentation and enforced at the
// driver's instantiation.

namespace welder {

template <class B>
concept backend =
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
// that is a class type binds via bind_type, and a nested namespace recurses.
template <backend B, class T>
auto bind_type(typename B::module_type& m, const char* name);
template <backend B, class E>
auto bind_enum(typename B::module_type& m, const char* name);
template <backend B, std::meta::info Ns>
void bind_namespace_driver(typename B::module_type& m);

// Reflect over enum E and register it via backend B onto module `m`: its
// docstring, then each enumerator that resolves as bound (an enumerator honors the
// enum's policy and its own exclude/include marks, exactly like a data member).
// finish_enum lets the backend apply a whole-enum finalizer — e.g. exporting an
// unscoped enum's values into the enclosing scope, mirroring C++. `name` defaults
// to E's identifier. Returns the backend's enum handle.
template <backend B, class E>
auto bind_enum(typename B::module_type& m, const char* name) {
    constexpr lang L{B::language};
    static_assert(welder::welded_for(^^E, L),
                  "welder: bind<E>: enum E is not welded for this backend's "
                  "language; annotate it with [[=welder::weld(...)]]");
    const char* enum_name{
        name ? name : std::define_static_string(std::meta::identifier_of(^^E))};
    auto e{B::template make_enum<E>(m, enum_name, welder::doc_of<^^E>())};
    constexpr policy_kind pol{policy_of(^^E)};
    template for (constexpr auto en :
                  std::define_static_array(std::meta::enumerators_of(^^E))) {
        if constexpr (member_bound(en, L, pol))
            B::template add_enumerator<en>(e);
    }
    B::template finish_enum<E>(e);
    return e;
}

// Flatten the eligible public data members, methods and operators of Src onto the
// class handle `cls` (a handle for a type deriving from Src). Non-welded (mixin)
// bases are recursed *first* so that, on a name clash, the member declared closer
// to the derived type wins. Constructors are never flattened (the derived type
// provides its own). A welded base is skipped here — it binds natively, as a base
// of the class handle (see native_base_types).
template <backend B, std::meta::info Src, class Cls>
void bind_members(Cls& cls) {
    constexpr lang L{B::language};
    constexpr auto ctx{std::meta::access_context::unchecked()};

    template for (constexpr auto base :
                  std::define_static_array(welder::public_bases(Src))) {
        if constexpr (!welder::welded_for(base, L))
            bind_members<B, base>(cls);
    }

    constexpr policy_kind pol{policy_of(Src)};

    template for (constexpr auto mem : std::define_static_array(
                      std::meta::nonstatic_data_members_of(Src, ctx))) {
        if constexpr (std::meta::is_public(mem) && member_bound(mem, L, pol)) {
            welder::assert_member_bindable<B, mem, L>();
            B::template add_field<mem>(cls);
        }
    }

    template for (constexpr auto fn :
                  std::define_static_array(std::meta::members_of(Src, ctx))) {
        if constexpr (is_bindable_method(fn, L, pol)) {
            welder::assert_callable_bindable<B, fn, L>();
            if constexpr (std::meta::is_static_member(fn))
                B::template add_static_method<fn>(cls);
            else
                B::template add_method<fn>(cls);
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

// Reflect over T and register it via backend B onto module `m`:
//   * native inheritance from T's nearest welded ancestors (each a base of the
//     class handle; bind those bases separately, before T);
//   * the default constructor (if any), each public non-copy/move constructor,
//     and — for a baseless aggregate whose fields all bind — a synthesized field
//     constructor;
//   * public data members / methods / operators that resolve as bound, plus the
//     eligible members of every non-welded public base, flattened in.
// `name` defaults to T's identifier. Returns the backend's class handle so callers
// can chain further registrations.
template <backend B, class T>
auto bind_type(typename B::module_type& m, const char* name) {
    constexpr lang L{B::language};
    static_assert(welder::welded_for(^^T, L),
                  "welder: bind<T>: T is not welded for this backend's language; "
                  "annotate it with [[=welder::weld(...)]]");
    constexpr auto ctx{std::meta::access_context::unchecked()};

    const char* cls_name{
        name ? name : std::define_static_string(std::meta::identifier_of(^^T))};

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
    bind_members<B, ^^T>(cls);

    return cls;
}

// Reflect over a whole namespace and expose its members on module `m`. `weld`
// makes an entity a candidate; the namespace `policy` (default automatic) and
// per-member marks then resolve what binds. Classes bind via bind_type, free
// functions and namespace variables become module attributes, and a nested
// namespace holding bound content becomes a submodule (recursed under its own
// policy). Members are visited in declaration order.
template <backend B, std::meta::info Ns>
void bind_namespace_driver(typename B::module_type& m) {
    static_assert(std::meta::is_namespace(Ns),
                  "welder: bind_namespace<Ns>: Ns must reflect a namespace");
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
                bind_type<B, typename [:mem:]>(m, nullptr);
        } else if constexpr (std::meta::is_type(mem) && std::meta::is_enum_type(mem)) {
            if constexpr (welder::welded_for(mem, L) && member_bound(mem, L, pol))
                bind_enum<B, typename [:mem:]>(m, nullptr);
        } else if constexpr (std::meta::is_function(mem)) {
            if constexpr (welder::welded_for(mem, L) && member_bound(mem, L, pol)) {
                welder::assert_callable_bindable<B, mem, L>();
                B::template add_function<mem>(m);
            }
        } else if constexpr (std::meta::is_variable(mem)) {
            if constexpr (welder::welded_for(mem, L) && member_bound(mem, L, pol)) {
                welder::assert_member_bindable<B, mem, L>();
                B::template add_variable<mem>(m, session);
            }
        } else if constexpr (std::meta::is_namespace(mem)) {
            // A nested namespace resolves like a leaf under the parent policy (but
            // is never welded): automatic recurses unless excluded, opt_in only if
            // included. It becomes a submodule when it holds bound content (then
            // resolved under its *own* policy).
            if constexpr (member_bound(mem, L, pol) && namespace_has_bound(mem, L)) {
                auto sub{B::add_submodule(
                    m, std::define_static_string(std::meta::identifier_of(mem)))};
                bind_namespace_driver<B, mem>(sub);
            }
        }
    }

    B::close_module(m, session);
}

// Fill an existing module out of top-level namespace `Ns`: pre hook, bind the
// namespace, post hook. `Ns` is asserted top-level (its name is the module name).
template <backend B, std::meta::info Ns, class Pre, class Post>
void build_module_driver(typename B::module_type& m, Pre pre, Post post) {
    static_assert(std::meta::is_namespace(Ns),
                  "welder: build_module<Ns>: Ns must reflect a namespace");
    static_assert(std::meta::parent_of(Ns) == ^^::,
                  "welder: build_module<Ns>: Ns must be a top-level namespace (its "
                  "name is meant to be the module name)");
    pre(m);
    bind_namespace_driver<B, Ns>(m);
    post(m);
}

} // namespace detail
} // namespace welder
