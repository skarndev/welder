#pragma once
#include <welder/detail/config.hpp>
#include <welder/lang.hpp>

// The annotation vocabulary users attach to their types. Like <welder/lang.hpp>,
// this is std-include-free so it can be exported by the `welder` module.

WELDER_EXPORT namespace welder {

// Languages are tracked as bits in a mask so a spec can name an arbitrary
// subset. For exclude/include specs a mask of 0 is the sentinel meaning
// "all welded languages".
consteval unsigned lang_bit(lang l) {
    return 1u << static_cast<unsigned>(l);
}

template <class... Ls>
consteval unsigned lang_mask(Ls... ls) {
    return (0u | ... | lang_bit(ls));
}

// --- weld: the type-level annotation declaring target languages -------------

struct weld_spec {
    unsigned mask = 0;
};

// Usage: [[=welder::weld(welder::lang::py, welder::lang::lua)]]
template <class... Ls>
consteval weld_spec weld(Ls... ls) {
    return weld_spec{lang_mask(ls...)};
}

// --- policy: how greedily members are reflected -----------------------------

enum class policy_kind : unsigned char {
    automatic, // reflect every member unless explicitly excluded (default)
    opt_in,    // reflect only members explicitly marked include
};

struct policy_spec {
    policy_kind kind = policy_kind::automatic;
};

namespace policy {
// Usage: [[=welder::policy::automatic]] (`auto` is reserved, hence the spelling)
inline constexpr policy_spec automatic{policy_kind::automatic};
inline constexpr policy_spec opt_in{policy_kind::opt_in};
} // namespace policy

// --- mark: member-level include/exclude -------------------------------------
//
// Each marker is a constexpr object usable bare as an annotation (applies to all
// languages) or called with languages to scope it:
//   [[=welder::mark::exclude]]                    -> all languages
//   [[=welder::mark::exclude(welder::lang::lua)]] -> lua only

struct exclude_spec {
    unsigned mask = 0; // 0 == all languages

    template <class... Ls>
    consteval exclude_spec operator()(Ls... ls) const {
        return exclude_spec{lang_mask(ls...)};
    }
};

struct include_spec {
    unsigned mask = 0; // 0 == all languages

    template <class... Ls>
    consteval include_spec operator()(Ls... ls) const {
        return include_spec{lang_mask(ls...)};
    }
};

namespace mark {
inline constexpr exclude_spec exclude{};
inline constexpr include_spec include{};
} // namespace mark

// --- doc: human-readable documentation --------------------------------------
//
// [[=welder::doc("text")]] attaches a docstring to a namespace, class, function,
// or function parameter. Backends surface it in the target language (e.g. a
// Python __doc__, with parameter docs folded into the function's docstring).
//
// The text is stored *inline* (a char array) rather than as a `const char*` to a
// string literal: a pointer-to-literal is not a permitted annotation value
// (gcc-16 rejects it as a non-structural constant), whereas an inline array is.
// Reading the text back — matching any `doc_spec<N>` and folding parameter docs
// into a styled docstring — is the reflection layer's job (<welder/doc.hpp>);
// this header stays std-include-free so the module can export the vocabulary.

// A string literal captured by value, length included, so it can live in a
// structural annotation constant. (decltype(sizeof(0)) is size_t without a
// standard-library include — see the std-free constraint above.)
template <decltype(sizeof(0)) N>
struct fixed_string {
    char data[N]{};
    consteval fixed_string(const char (&s)[N]) {
        for (decltype(sizeof(0)) i{0}; i < N; ++i)
            data[i] = s[i];
    }
};

template <decltype(sizeof(0)) N>
struct doc_spec {
    fixed_string<N> text;
};

// Usage: [[=welder::doc("Summary line.")]]
template <decltype(sizeof(0)) N>
consteval doc_spec<N> doc(const char (&s)[N]) {
    return doc_spec<N>{fixed_string<N>{s}};
}

// A return value is not a reflectable entity, so its documentation is attached to
// the *function* as a distinct annotation type — `doc` there is already the
// function's summary. The reflection layer tells the two apart by spec type, the
// same way it ignores `weld`/`mark` on a documented entity.
//
// Usage: [[=welder::returns("what the call yields")]] on a function.
template <decltype(sizeof(0)) N>
struct return_doc_spec {
    fixed_string<N> text;
};

template <decltype(sizeof(0)) N>
consteval return_doc_spec<N> returns(const char (&s)[N]) {
    return return_doc_spec<N>{fixed_string<N>{s}};
}

} // namespace welder
