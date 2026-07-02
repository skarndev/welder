#pragma once
#include <meta>
#include <type_traits>

#include <welder/bind_traits.hpp> // selection: is_bindable_constructor/method/... + public_bases
#include <welder/doc.hpp>         // doc_of (namespace docs handed to the emitter)
#include <welder/reflect.hpp>     // resolution: member_bound

// The C++ documentation walker + the doc_emitter interface — the docs sibling of
// <welder/backend.hpp>'s binding driver.
//
// Binding and documenting share the traversal *pattern* (a generic driver over a
// pluggable, statically-polymorphic emission policy) but not the *gates*. Binding
// exposes an entity to a foreign language, which costs a registration, so it is
// opt-in via `weld` and shaped by `policy`. C++ needs no registration: every
// public entity is already part of the C++ API by virtue of existing. So the doc
// walk targets the `lang::cxx_doc` pseudo-language with degenerate resolution:
//   * `weld` never gates (an unwelded, C++-only type is still documented);
//   * `policy` does not apply — the walk is always automatic (public = documented;
//     `policy::opt_in` narrows a *binding* surface, not the C++ API);
//   * the one control is mark::exclude(lang::cxx_doc) — or a bare mark::exclude,
//     whose "all languages" sentinel covers cxx_doc too — hiding an entity from
//     the generated reference (e.g. a `detail` namespace).
// The `doc`/`returns` annotation *content* is read by the emitter through
// <welder/doc.hpp>, which was always language-agnostic; docs that binding
// backends must drop (variable docs, per-enumerator docs) surface here.
//
// Limitations (v1, deliberate): uninstantiated templates and concepts are skipped
// — P2996 permits annotations_of only on types/variables/functions/namespaces/
// enumerators/parameters, so a template's doc text is unreachable without an
// instantiation (and its members cannot be enumerated at all). Free operators,
// static data members and nested class types are TODO.
//
// Like <welder/backend.hpp>, this depends on the welder vocabulary (lang,
// policy_kind, ...) but does NOT include <welder/annotations.hpp>: provide the
// vocabulary first (`import welder;` or `#include <welder/welder.hpp>`), then this.
//
// ── The doc_emitter contract ────────────────────────────────────────────────
// An emitter is an object (stateful, unlike a backend — it accumulates output)
// passed by reference through the walk. It derives names, signatures and doc
// text itself from the reflection it receives (via <meta> and <welder/doc.hpp>);
// the walker only decides *what* is documented and in what order. E provides:
//
//   Namespaces (checked by the concept):
//     void open_namespace(const char* name, const char* doc);  // doc may be null
//     void close_namespace(const char* name);
//
//   Classes (templated on a reflection — contract-by-documentation, enforced
//   at the walker's instantiation, like the backend's per-member hooks):
//     template <std::meta::info Class> void open_class();
//     template <std::meta::info Class> void emit_default_ctor();
//     template <std::meta::info Ctor>  void emit_constructor();
//     template <std::meta::info Mem>   void emit_field();
//     template <std::meta::info Fn>    void emit_method();     // static told apart
//                                                              // via is_static_member
//     template <std::meta::info Fn>    void emit_operator();
//     template <std::meta::info Class> void close_class();
//
//   Enums:
//     template <std::meta::info Enum> void open_enum();
//     template <std::meta::info En>   void emit_enumerator();
//     template <std::meta::info Enum> void close_enum();
//
//   Namespace members:
//     template <std::meta::info Fn>  void emit_function();
//     template <std::meta::info Var> void emit_variable();

namespace welder::docs {

template <class E>
concept doc_emitter = requires(E& e, const char* s) {
    e.open_namespace(s, s);
    e.close_namespace(s);
};

// The whole resolution rule for C++ docs: honoring only exclude marks (the
// cxx_doc bit or the all-languages sentinel), under a forced automatic policy.
consteval bool doc_bound(std::meta::info mem) {
    return member_bound(mem, lang::cxx_doc, policy_kind::automatic);
}

// A namespace member the walker can document: the same concrete leaf kinds the
// binding driver handles (class / enum / function / variable — templates and
// concepts fail all these predicates and are skipped), minus free operators.
consteval bool is_documentable_entity(std::meta::info mem) {
    return welder::detail::is_bindable_kind(mem) &&
           !(std::meta::is_function(mem) && std::meta::is_operator_function(mem));
}

// Whether `ns` holds anything that would be documented, directly or nested —
// an empty (or fully excluded) namespace is pruned rather than emitted.
consteval bool namespace_has_docs(std::meta::info ns) {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    for (auto mem : std::meta::members_of(ns, ctx)) {
        if (std::meta::is_namespace(mem)) {
            if (doc_bound(mem) && namespace_has_docs(mem))
                return true;
        } else if (is_documentable_entity(mem) && doc_bound(mem)) {
            return true;
        }
    }
    return false;
}

// Document enum `Enum`: each enumerator that is not excluded for cxx_doc, in
// declaration order. Mirrors the binding driver's bind_enum, minus weld/policy.
template <std::meta::info Enum, doc_emitter E>
void document_enum(E& e) {
    e.template open_enum<Enum>();
    template for (constexpr auto en :
                  std::define_static_array(std::meta::enumerators_of(Enum))) {
        if constexpr (doc_bound(en))
            e.template emit_enumerator<en>();
    }
    e.template close_enum<Enum>();
}

// Document class `Class`: constructors (default + each public non-copy/move one,
// the same eligibility the binding driver uses), then public data members,
// methods and operators that are not excluded for cxx_doc. Bases are *not*
// flattened: C++ docs show real inheritance, so the emitter lists the public
// bases on the class head (via public_bases) and each base documents its own
// members wherever *it* is documented.
template <std::meta::info Class, doc_emitter E>
void document_class(E& e) {
    constexpr auto ctx{std::meta::access_context::unchecked()};
    constexpr lang L{lang::cxx_doc};
    constexpr policy_kind pol{policy_kind::automatic};

    e.template open_class<Class>();

    if constexpr (std::is_default_constructible_v<typename [:Class:]>)
        e.template emit_default_ctor<Class>();
    template for (constexpr auto ctor : std::define_static_array(
                      std::meta::members_of(Class, ctx))) {
        if constexpr (welder::detail::is_bindable_constructor(ctor))
            e.template emit_constructor<ctor>();
    }

    template for (constexpr auto mem : std::define_static_array(
                      std::meta::nonstatic_data_members_of(Class, ctx))) {
        if constexpr (std::meta::is_public(mem) && doc_bound(mem))
            e.template emit_field<mem>();
    }

    template for (constexpr auto fn : std::define_static_array(
                      std::meta::members_of(Class, ctx))) {
        if constexpr (welder::detail::is_bindable_method(fn, L, pol))
            e.template emit_method<fn>();
        else if constexpr (welder::detail::is_operator_candidate(fn, L, pol))
            // Unlike binding, no backend name-map gates this: C++ docs show every
            // public operator under its own name.
            e.template emit_operator<fn>();
    }

    e.template close_class<Class>();
}

// Document namespace `Ns` and everything reachable under it, in declaration
// order. The entry point for emitters; recurses into nested namespaces that hold
// documentable content (pruning empty ones, like the binding driver's submodule
// rule — but with no weld gate anywhere).
template <std::meta::info Ns, doc_emitter E>
void document_namespace(E& e) {
    static_assert(std::meta::is_namespace(Ns),
                  "welder: document_namespace<Ns>: Ns must reflect a namespace");
    constexpr auto ctx{std::meta::access_context::unchecked()};

    e.open_namespace(std::define_static_string(std::meta::identifier_of(Ns)),
                     welder::doc_of<Ns>());

    template for (constexpr auto mem :
                  std::define_static_array(std::meta::members_of(Ns, ctx))) {
        if constexpr (std::meta::is_type(mem) && std::meta::is_class_type(mem)) {
            if constexpr (doc_bound(mem))
                document_class<mem>(e);
        } else if constexpr (std::meta::is_type(mem) &&
                             std::meta::is_enum_type(mem)) {
            if constexpr (doc_bound(mem))
                document_enum<mem>(e);
        } else if constexpr (std::meta::is_function(mem)) {
            if constexpr (!std::meta::is_operator_function(mem) && doc_bound(mem))
                e.template emit_function<mem>();
        } else if constexpr (std::meta::is_variable(mem)) {
            if constexpr (doc_bound(mem))
                e.template emit_variable<mem>();
        } else if constexpr (std::meta::is_namespace(mem)) {
            if constexpr (doc_bound(mem) && namespace_has_docs(mem))
                document_namespace<mem>(e);
        }
    }

    e.close_namespace(std::define_static_string(std::meta::identifier_of(Ns)));
}

} // namespace welder::docs
