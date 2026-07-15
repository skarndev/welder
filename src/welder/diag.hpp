#pragma once

/** @file
    welder's consteval diagnostics, collected in one place: every hand-rolled
    compile-time error the library raises beyond a `static_assert` is one of
    the exception types below, **thrown during constant evaluation** (a C++26
    constexpr exception). An uncaught constexpr exception renders the program
    ill-formed with the exception's type *and* the carried `what` prose
    verbatim in the compiler's `uncaught exception '…'` diagnostic — one
    error, a greppable type name, and a real punctuation-and-spaces message,
    where the older undefined-function-anchor idiom produced two errors with
    the message crammed into an identifier.

    Each type carries its canonical message as a default member initializer,
    so a raise site is just `throw diag::bare_mark_only{};` — the message has
    a single source of truth here.

    Like the vocabulary headers (`lang.hpp`, `annotations.hpp`), this header
    is **std-include-free** — the messages are plain `const char*`, core
    language only — so the vocabulary may include it and a future
    `import welder;` module wrapper can re-export it without leaking std into
    importers.
*/

namespace welder::inline v0::diag {

/** Thrown by `lang_bit` (`<welder/annotations.hpp>`) when a `lang` value lies
    past the 32-bit language-mask width — instead of an opaque shift-overflow
    error. */
struct lang_out_of_mask_range {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: this lang value is not a language-mask bit index (it lies "
        "past the 32-bit mask); mint user languages with "
        "welder::user_lang<Slot>, which cannot go out of range";
};

/** Thrown by `validate_return_policy` (`<welder/reflect.hpp>`) — every rod
    runs it at its per-overload bind site — when a reference-category
    `return_policy` meets a by-value return. */
struct dangling_return_policy {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: return_policy requests a reference-category policy "
        "(rv::reference / rv::reference_internal) on a callable returning BY "
        "VALUE - the target-language view would reference a destroyed "
        "temporary; drop the policy, or return a pointer/reference";
};

/** Thrown by `member_bound` (`<welder/reflect.hpp>`) when a member carries a
    bare `[[=welder::mark::only]]` — "only, for every language" restricts
    nothing, so the mark must be called. */
struct bare_mark_only {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: a bare [[=welder::mark::only]] is meaningless ('only, for "
        "every language' restricts nothing); call it with the languages the "
        "member may bind for: mark::only(welder::lang::py, ...)";
};

/** Thrown by `virtual_slot` (`<welder/rods/python/trampoline.hpp>`, reached
    through `WELDER_PY_OVERRIDE_AS`) when no overridable virtual of the class
    matches the requested name and function type. */
struct no_matching_virtual_slot {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: no overridable virtual of this class matches the given name "
        "and function type; check the spelling and the FULL function type - "
        "parameter types and trailing cv/ref qualifiers included - against "
        "the base's declaration";
};

/** Thrown by `member_access_admitted` (`<welder/bind_traits.hpp>`) when a
    resolution still declares its optional `protected_participates` hook under
    the superseded two-argument signature — which would otherwise be silently
    ignored (the detection is a `requires` probe). */
struct stale_hook_signature {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: this resolution's protected_participates hook gained a "
        "trailing `std::meta::info bound_into` parameter (the entity whose "
        "binding receives the member); update its signature to "
        "(mem, lang, bound_into)";
};

} // namespace welder::inline v0::diag