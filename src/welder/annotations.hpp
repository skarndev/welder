#pragma once
#include <welder/detail/config.hpp>
#include <welder/lang.hpp>

/** @file
    The annotation vocabulary users attach to their types — `weld`, `policy`,
    `mark`, `doc`, `returns`, `tparam`.

    Like `<welder/lang.hpp>`, this header is std-include-free so it can be exported
    by the `welder` module.
*/

WELDER_EXPORT namespace welder {

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

// --- weld: the type-level annotation declaring target languages -------------

/** The stored form of a `weld` annotation: the mask of target languages. */
struct weld_spec {
    unsigned mask = 0; /**< The languages this entity is welded for. */
};

/** Build a `weld` annotation naming the target languages.

    Usage: `[[=welder::weld(welder::lang::py, welder::lang::lua)]]`.

    @tparam Ls the language enum types (deduced).
    @param ls  the target languages this entity is exposed to.
    @return a weld_spec carrying the language mask.
*/
template <class... Ls>
consteval weld_spec weld(Ls... ls) {
    return weld_spec{lang_mask(ls...)};
}

// --- policy: how greedily members are reflected -----------------------------

/** How greedily a type's members are reflected for binding. */
enum class policy_kind : unsigned char {
    automatic, /**< Reflect every member unless explicitly excluded (default). */
    opt_in,    /**< Reflect only members explicitly marked `include`. */
};

/** The stored form of a `policy` annotation. */
struct policy_spec {
    policy_kind kind = policy_kind::automatic; /**< The chosen policy. */
};

/** The `policy` annotation values.

    Usage: `[[=welder::policy::opt_in]]` (`auto` is reserved, hence `automatic`).
*/
namespace policy {
inline constexpr policy_spec automatic{policy_kind::automatic}; /**< @see policy_kind::automatic */
inline constexpr policy_spec opt_in{policy_kind::opt_in};       /**< @see policy_kind::opt_in */
} // namespace policy

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
    the #trust_bindable variable template. Usable bare (all languages) or scoped,
    like exclude_spec.
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

/** The type-level trust customization point: specialize to `true` to trust @a T
    wherever it appears (member, parameter, return, container element, …).

    A plain `bool`, so it trusts @a T for every language at once. Usage (at
    namespace scope, before binding a type that uses @a T):
    @code
    template <> inline constexpr bool welder::trust_bindable<Foo> = true;
    @endcode

    @tparam T the type to trust everywhere. See trust_bindable_spec for the
              per-member granularity.
*/
template <class T>
inline constexpr bool trust_bindable = false;

/** The bare `mark` annotation objects — use directly or call to scope by language. */
namespace mark {
inline constexpr exclude_spec exclude{};                 /**< @see exclude_spec */
inline constexpr include_spec include{};                 /**< @see include_spec */
inline constexpr trust_bindable_spec trust_bindable{};   /**< @see trust_bindable_spec */
} // namespace mark

// --- doc: human-readable documentation --------------------------------------

/** A string literal captured by value, length included, so it can live in a
    structural annotation constant.

    Doc text is stored *inline* (a char array) rather than as a `const char*` to a
    string literal: a pointer-to-literal is not a permitted annotation value
    (gcc-16 rejects it as a non-structural constant), whereas an inline array is.

    @tparam N the literal's length including the terminator (deduced).
    @note `decltype(sizeof(0))` is `size_t` without a standard-library include —
          see the std-free constraint on this header.
*/
template <decltype(sizeof(0)) N>
struct fixed_string {
    char data[N]{}; /**< The captured characters, including the terminator. */
    /** Capture the characters of @a s. @param s the source string literal. */
    consteval fixed_string(const char (&s)[N]) {
        for (decltype(sizeof(0)) i{0}; i < N; ++i)
            data[i] = s[i];
    }
};

/** The stored form of a `doc` annotation: a summary docstring.

    @tparam N the text length (deduced).
*/
template <decltype(sizeof(0)) N>
struct doc_spec {
    fixed_string<N> text; /**< The docstring text. */
};

/** Attach a docstring to a namespace, class, function, or function parameter.

    Backends surface it in the target language (e.g. a Python `__doc__`, with
    parameter docs folded into the function's docstring). Reading the text back is
    the reflection layer's job (`<welder/doc.hpp>`).

    Usage: `[[=welder::doc("Summary line.")]]`.

    @tparam N the text length (deduced).
    @param s  the docstring text.
    @return a doc_spec holding the text.
*/
template <decltype(sizeof(0)) N>
consteval doc_spec<N> doc(const char (&s)[N]) {
    return doc_spec<N>{fixed_string<N>{s}};
}

/** The stored form of a `returns` annotation: a function's return-value doc.

    A return value is not a reflectable entity, so its documentation is attached to
    the *function* as a distinct annotation type — `doc` there is already the
    function's summary. The reflection layer tells the two apart by spec type.

    @tparam N the text length (deduced).
*/
template <decltype(sizeof(0)) N>
struct return_doc_spec {
    fixed_string<N> text; /**< The return-value documentation. */
};

/** Document a function's return value.

    Usage: `[[=welder::returns("what the call yields")]]` on a function.

    @tparam N the text length (deduced).
    @param s  the return-value documentation.
    @return a return_doc_spec holding the text.
*/
template <decltype(sizeof(0)) N>
consteval return_doc_spec<N> returns(const char (&s)[N]) {
    return return_doc_spec<N>{fixed_string<N>{s}};
}

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
template <decltype(sizeof(0)) N, decltype(sizeof(0)) M>
struct tparam_spec {
    fixed_string<N> name; /**< The documented template parameter's name. */
    fixed_string<M> text; /**< Its documentation. */
};

/** Document a template parameter (repeatable, ordered).

    Usage: `[[=welder::tparam("T", "what T is")]]` on a class/function template.

    @tparam N the parameter-name length (deduced).
    @tparam M the text length (deduced).
    @param name the template parameter's name, matching the declaration.
    @param text its documentation.
    @return a tparam_spec pairing the name with the text.
*/
template <decltype(sizeof(0)) N, decltype(sizeof(0)) M>
consteval tparam_spec<N, M> tparam(const char (&name)[N], const char (&text)[M]) {
    return tparam_spec<N, M>{fixed_string<N>{name}, fixed_string<M>{text}};
}

// --- weld_as: force an entity's target-language name verbatim ----------------

/** The stored form of a `weld_as` annotation: a forced target-language name.

    Where a code-style transformer (see `<welder/naming.hpp>`) reshapes an
    entity's C++ identifier into the target language's convention, `weld_as` is the
    ultimate override — the string is used **verbatim**, bypassing the transformer
    entirely. Scope it to one language or (bare) to all of them, so a member can
    read `process` in Python and `Process` in Lua at once:
    @code
    [[=welder::weld_as("id")]]                     // every language
    [[=welder::weld_as(welder::lang::py, "id")]]   // Python only
    @endcode

    The name is captured inline (a @ref fixed_string), like `doc`, so it can live in
    a structural annotation constant.
    @tparam N the name length including the terminator (deduced).
*/
template <decltype(sizeof(0)) N>
struct weld_as_spec {
    unsigned mask = 0;    /**< The languages to rename for; `0` == all languages. */
    fixed_string<N> name; /**< The verbatim target-language name. */
};

/** Force @a s as the target name in every welded language.

    Usage: `[[=welder::weld_as("do_thing")]]`.
    @tparam N the name length (deduced).
    @param s the verbatim name.
    @return a weld_as_spec covering all languages.
*/
template <decltype(sizeof(0)) N>
consteval weld_as_spec<N> weld_as(const char (&s)[N]) {
    return weld_as_spec<N>{0u, fixed_string<N>{s}};
}

/** Force @a s as the target name in language @a l only.

    Usage: `[[=welder::weld_as(welder::lang::py, "do_thing")]]`. Repeat the
    annotation to give an entity a different verbatim name per language.
    @tparam N the name length (deduced).
    @param l the language this override applies to.
    @param s the verbatim name.
    @return a weld_as_spec scoped to @a l.
*/
template <decltype(sizeof(0)) N>
consteval weld_as_spec<N> weld_as(lang l, const char (&s)[N]) {
    return weld_as_spec<N>{lang_bit(l), fixed_string<N>{s}};
}

} // namespace welder
