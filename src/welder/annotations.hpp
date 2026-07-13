#pragma once
#include <welder/lang.hpp>

/** @file
    The annotation vocabulary users attach to their types — `weld`, `policy`,
    `mark`, `doc`, `returns`, `tparam`.

    Users spell these through the factory functions and the `policy` / `mark`
    objects below; the *stored forms* each one produces (the `*_spec` structs the
    reflection layer reads back) are implementation detail, tucked into
    `welder::detail` so they don't crowd the public `welder::` namespace.

    Like `<welder/lang.hpp>`, this header is kept std-include-free so a future
    `welder` C++20 module can re-export it without leaking std into importers.
*/

namespace welder {

/** `std::size_t` named without a standard-library include, keeping this
    vocabulary header std-free (see the file note). `sizeof` yields `std::size_t`
    by definition, so `decltype(sizeof(0))` *is* that type — this alias just gives
    it a readable name for the array-length template parameters below. */
using size_type = decltype(sizeof(0));

/** The single-language bit for @a l within a language mask.

    Languages are tracked as bits in a mask so a spec can name an arbitrary
    subset.
    @param l the language.
    @return `1u << index-of(l)`.
*/
consteval unsigned lang_bit(lang l) {
    return 1u << static_cast<unsigned>(l);
}

/** The mask naming the languages @a ls.

    @tparam Ls the language enum types (deduced).
    @param ls  the languages to combine.
    @return the OR of each language's bit; `0` for an empty pack — which, on an
            exclude/include/trust spec, is the sentinel meaning "all welded
            languages".
*/
template <class... Ls>
consteval unsigned lang_mask(Ls... ls) {
    return (0u | ... | lang_bit(ls));
}

/** How greedily a type's members are reflected for binding. */
enum class policy_kind : unsigned char {
    automatic, /**< Reflect every member unless explicitly excluded (default). */
    opt_in,    /**< Reflect only members explicitly marked `include`. */
};

/** The stored forms of the annotation vocabulary.

    Each `*_spec` here is what a factory below (`weld`, `doc`, …) or a `policy` /
    `mark` object produces and what the reflection layer extracts back off an
    entity's annotations. Users never name these types — they spell the factories —
    so they live in `detail` to keep `welder::` uncluttered.
*/
namespace detail {

// --- weld: the type-level annotation declaring target languages -------------

/** The stored form of a `weld` annotation: the mask of target languages. */
struct weld_spec {
    unsigned mask = 0; /**< The languages this entity is welded for. */
};

// --- policy: how greedily members are reflected -----------------------------

/** The stored form of a `policy` annotation. */
struct policy_spec {
    policy_kind kind = policy_kind::automatic; /**< The chosen policy. */
};

// --- mark: member-level include/exclude -------------------------------------

/** The stored form of an `exclude` mark: the languages a member is hidden from.

    Each marker is a constexpr object usable bare as an annotation (applies to all
    languages) or called with languages to scope it:
    @code
    [[=welder::mark::exclude]]                    // all languages
    [[=welder::mark::exclude(welder::lang::lua)]] // lua only
    @endcode
*/
struct exclude_spec {
    unsigned mask = 0; /**< The languages to exclude from; `0` == all languages. */

    /** Scope the exclusion to specific languages.
        @tparam Ls the language enum types (deduced).
        @param ls  the languages to exclude from.
        @return a scoped exclude_spec. */
    template <class... Ls>
    consteval exclude_spec operator()(Ls... ls) const {
        return exclude_spec{lang_mask(ls...)};
    }
};

/** The stored form of an `include` mark: the languages a member is opted into.

    Meaningful under `policy::opt_in`. Usable bare (all languages) or scoped, like
    exclude_spec.
*/
struct include_spec {
    unsigned mask = 0; /**< The languages to include for; `0` == all languages. */

    /** Scope the inclusion to specific languages.
        @tparam Ls the language enum types (deduced).
        @param ls  the languages to include for.
        @return a scoped include_spec. */
    template <class... Ls>
    consteval include_spec operator()(Ls... ls) const {
        return include_spec{lang_mask(ls...)};
    }
};

// --- trust_bindable: vouch that a type is representable outside welder's view --

/** The stored form of a `trust_bindable` member mark.

    welder's bindability gate (see `<welder/bindable.hpp>`) rejects a
    program-defined type that is not welded, because it cannot see a registration
    made outside welder — a hand-written pybind11 `class_`, a third-party library's
    bindings. `trust_bindable` is the user's vouch that such a registration exists
    (or that the backend can otherwise convert the type), suppressing the gate. The
    user then owns that registration; welder emits the binding trusting it is there.

    This is the *member* granularity — it trusts this member's type(s): the data
    member's type, or a function's whole signature. For the *type* granularity see
    the #welder::trust_bindable variable template. Usable bare (all languages) or
    scoped, like exclude_spec.
*/
struct trust_bindable_spec {
    unsigned mask = 0; /**< The languages to trust for; `0` == all languages. */

    /** Scope the vouch to specific languages.
        @tparam Ls the language enum types (deduced).
        @param ls  the languages to trust for.
        @return a scoped trust_bindable_spec. */
    template <class... Ls>
    consteval trust_bindable_spec operator()(Ls... ls) const {
        return trust_bindable_spec{lang_mask(ls...)};
    }
};

// --- doc: human-readable documentation --------------------------------------

/** A string literal captured by value, length included, so it can live in a
    structural annotation constant.

    Doc text is stored *inline* (a char array) rather than as a `const char*` to a
    string literal: a pointer-to-literal is not a permitted annotation value
    (gcc-16 rejects it as a non-structural constant), whereas an inline array is.

    @tparam N the literal's length including the terminator (deduced).
*/
template <size_type N>
struct fixed_string {
    char data[N]{}; /**< The captured characters, including the terminator. */
    /** Capture the characters of @a s. @param s the source string literal. */
    consteval fixed_string(const char (&s)[N]) {
        for (size_type i{0}; i < N; ++i)
            data[i] = s[i];
    }
};

/** The stored form of a `doc` annotation: a summary docstring.

    @tparam N the text length (deduced).
*/
template <size_type N>
struct doc_spec {
    fixed_string<N> text; /**< The docstring text. */
};

/** The stored form of a `returns` annotation: a function's return-value doc.

    A return value is not a reflectable entity, so its documentation is attached to
    the *function* as a distinct annotation type — `doc` there is already the
    function's summary. The reflection layer tells the two apart by spec type.

    @tparam N the text length (deduced).
*/
template <size_type N>
struct return_doc_spec {
    fixed_string<N> text; /**< The return-value documentation. */
};

/** The stored form of a `tparam` annotation: one template parameter's doc.

    A template parameter is not a reflectable entity either (P2996 exposes no
    template-parameter API), so — like a return value — its documentation rides on
    the *template itself*, as a repeatable annotation naming the parameter:
    @code
    template <class K, class V>
    struct [[=welder::doc("A dictionary."),
             =welder::tparam("K", "the key type"),
             =welder::tparam("V", "the mapped type")]] Dict { ... };
    @endcode

    Consumers: the Doxygen INPUT_FILTER renders each as a `@tparam K text` line;
    reflection reads them back via `tparam_docs()` (`<welder/doc.hpp>`) — off an
    *instantiation*, since P2996 refuses `annotations_of` on the uninstantiated
    template.

    @tparam N the parameter-name length (deduced).
    @tparam M the text length (deduced).
*/
template <size_type N, size_type M>
struct tparam_spec {
    fixed_string<N> name; /**< The documented template parameter's name. */
    fixed_string<M> text; /**< Its documentation. */
};

// --- weld_as: force an entity's target-language name verbatim ----------------

/** The stored form of a `weld_as` annotation: a forced target-language name.

    Where a code-style transformer (see `<welder/naming.hpp>`) reshapes an
    entity's C++ identifier into the target language's convention, `weld_as` is the
    ultimate override — the string is used **verbatim**, bypassing the transformer
    entirely. The name is always the **last** argument; any languages naming it come
    first. Give no language and it covers all of them; give one or several to scope
    it, and repeat the annotation to use a different name per language — so a member
    can read `process` in Python and `Process` in Lua at once:
    @code
    [[=welder::weld_as("id")]]                                   // every language
    [[=welder::weld_as(welder::lang::py, "id")]]                 // Python only
    [[=welder::weld_as(welder::lang::py, welder::lang::lua, "id")]] // Python + Lua
    @endcode

    The name is captured inline (a @ref fixed_string), like `doc`, so it can live in
    a structural annotation constant.
    @tparam N the name length including the terminator (deduced).
*/
template <size_type N>
struct weld_as_spec {
    unsigned mask = 0;    /**< The languages to rename for; `0` == all languages. */
    fixed_string<N> name; /**< The verbatim target-language name. */
};

} // namespace detail

// --- weld: the type-level annotation declaring target languages -------------

/** Build a `weld` annotation naming the target languages.

    Usage: `[[=welder::weld(welder::lang::py, welder::lang::lua)]]`.

    @tparam Ls the language enum types (deduced).
    @param ls  the target languages this entity is exposed to.
    @return a weld_spec carrying the language mask.
*/
template <class... Ls>
consteval detail::weld_spec weld(Ls... ls) {
    return detail::weld_spec{lang_mask(ls...)};
}

// --- policy: how greedily members are reflected -----------------------------

/** The `policy` annotation values.

    Usage: `[[=welder::policy::opt_in]]` (`auto` is reserved, hence `automatic`).
*/
namespace policy {
inline constexpr detail::policy_spec automatic{policy_kind::automatic}; /**< @see policy_kind::automatic */
inline constexpr detail::policy_spec opt_in{policy_kind::opt_in};       /**< @see policy_kind::opt_in */
} // namespace policy

// --- mark: member-level include/exclude / trust_bindable --------------------

/** The type-level trust customization point: specialize to `true` to trust @a T
    wherever it appears (member, parameter, return, container element, …).

    A plain `bool`, so it trusts @a T for every language at once. Usage (at
    namespace scope, before binding a type that uses @a T):
    @code
    template <> inline constexpr bool welder::trust_bindable<Foo> = true;
    @endcode

    @tparam T the type to trust everywhere. See welder::detail::trust_bindable_spec
              for the per-member granularity.
*/
template <class T>
inline constexpr bool trust_bindable = false;

/** The bare `mark` annotation objects — use directly or call to scope by language. */
namespace mark {
inline constexpr detail::exclude_spec exclude{};               /**< @see welder::detail::exclude_spec */
inline constexpr detail::include_spec include{};               /**< @see welder::detail::include_spec */
inline constexpr detail::trust_bindable_spec trust_bindable{}; /**< @see welder::detail::trust_bindable_spec */
} // namespace mark

// --- doc: human-readable documentation --------------------------------------

/** Attach a docstring to a namespace, class, function, or function parameter.

    Backends surface it in the target language (e.g. a Python `__doc__`, with
    parameter docs folded into the function's docstring). Reading the text back is
    the reflection layer's job (`<welder/doc.hpp>`).

    Usage: `[[=welder::doc("Summary line.")]]`.

    @tparam N the text length (deduced).
    @param s  the docstring text.
    @return a doc_spec holding the text.
*/
template <size_type N>
consteval detail::doc_spec<N> doc(const char (&s)[N]) {
    return detail::doc_spec<N>{detail::fixed_string<N>{s}};
}

/** Document a function's return value.

    Usage: `[[=welder::returns("what the call yields")]]` on a function.

    @tparam N the text length (deduced).
    @param s  the return-value documentation.
    @return a return_doc_spec holding the text.
*/
template <size_type N>
consteval detail::return_doc_spec<N> returns(const char (&s)[N]) {
    return detail::return_doc_spec<N>{detail::fixed_string<N>{s}};
}

/** Document a template parameter (repeatable, ordered).

    Usage: `[[=welder::tparam("T", "what T is")]]` on a class/function template.

    @tparam N the parameter-name length (deduced).
    @tparam M the text length (deduced).
    @param name the template parameter's name, matching the declaration.
    @param text its documentation.
    @return a tparam_spec pairing the name with the text.
*/
template <size_type N, size_type M>
consteval detail::tparam_spec<N, M> tparam(const char (&name)[N], const char (&text)[M]) {
    return detail::tparam_spec<N, M>{detail::fixed_string<N>{name}, detail::fixed_string<M>{text}};
}

// --- weld_as: force an entity's target-language name verbatim ----------------

/** Force @a s as the target name in **every** welded language.

    The bare, all-languages form of @ref weld_as — the common case, spelled as its own
    overload (like `welder::weld()` with no languages) so a plain
    `[[=welder::weld_as("do_thing")]]` needs none of the variadic marker machinery. A
    single string argument is more specialized than the variadic form below, so it is
    the one chosen here.
    @tparam N the name length including the terminator (deduced).
    @param s the verbatim name.
    @return a weld_as_spec with mask `0` (all languages).
*/
template <size_type N>
consteval detail::weld_as_spec<N> weld_as(const char (&s)[N]) {
    return detail::weld_as_spec<N>{0u, detail::fixed_string<N>{s}};
}

namespace detail {
// The `weld_as(langs…, "name")` argument list is a run of `lang` markers followed
// by the verbatim name. A parameter pack cannot precede a deduced trailing string
// (the pack would not deduce), so `weld_as` takes one forwarding pack and these two
// helpers walk it: one ORs the leading markers, the other peels down to the name —
// binding the string by reference so its extent N survives (a by-value array decays
// to a pointer and loses its length). No std headers, so the vocabulary stays
// module-exportable.

/** The language mask of a `weld_as` argument list: the OR of its leading `lang`
    markers (the trailing name contributes nothing; no markers → `0` = all). */
consteval unsigned weld_as_mask() { return 0u; }
template <size_type N>
consteval unsigned weld_as_mask(const char (&)[N]) { return 0u; }
template <class... Rest>
consteval unsigned weld_as_mask(lang l, Rest&&... rest) {
    return lang_bit(l) | weld_as_mask(static_cast<Rest&&>(rest)...);
}

/** The verbatim name of a `weld_as` argument list: the trailing string, reached by
    dropping the leading `lang` markers (its extent N preserved by reference). */
template <size_type N>
consteval fixed_string<N> weld_as_name(const char (&s)[N]) { return fixed_string<N>{s}; }
template <class... Rest>
consteval auto weld_as_name(lang, Rest&&... rest) {
    return weld_as_name(static_cast<Rest&&>(rest)...);
}
} // namespace detail

/** Force a verbatim target name, optionally scoped to one or more languages.

    The verbatim name is the **last** argument; zero or more `lang` markers precede
    it. With no marker the name covers every welded language; with one or several it
    is scoped to those. Repeat the annotation to give an entity a different verbatim
    name per language.
    @code
    [[=welder::weld_as("do_thing")]]                          // all languages
    [[=welder::weld_as(welder::lang::py, "do_thing")]]        // Python only
    [[=welder::weld_as(welder::lang::py, welder::lang::lua, "do_thing")]] // both
    @endcode
    @tparam Args the leading `lang` markers then the `const char[N]` name (deduced).
    @param args the language markers (if any) followed by the verbatim name.
    @return a weld_as_spec carrying the mask of the named languages and the name.
*/
template <class... Args>
consteval auto weld_as(Args&&... args) {
    return detail::weld_as_spec{detail::weld_as_mask(static_cast<Args&&>(args)...),
                                detail::weld_as_name(static_cast<Args&&>(args)...)};
}

} // namespace welder