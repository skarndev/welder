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

/** Thrown by `validate_move_ctor_marks` (`<welder/bind_traits.hpp>`) — the
    carriage runs it for every welded class — when a **move constructor**
    carries an `include`/`only` mark. No target language has move semantics,
    so move construction never binds; the mark is an intent welder cannot
    honor, diagnosed rather than silently dropped. */
struct marked_move_constructor {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: a move constructor carries an include/only mark, but move "
        "construction never binds - no target language has move semantics. "
        "Remove the mark; copying is what crosses the boundary (the copy "
        "constructor binds as Python __copy__/__deepcopy__)";
};

/** Thrown by the `accessor_name` factory helper (`<welder/annotations.hpp>`)
    when a `getter`/`setter` explicit property name exceeds the spec's inline
    capacity (the spec is deliberately not length-templated — see
    `accessor_name_capacity`). */
struct accessor_name_too_long {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: this getter/setter explicit property name is longer than the "
        "64-character inline capacity; choose a shorter target-language name";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) when a
    `[[=welder::getter]]` sits on a function that is not a getter shape:
    it must be a **const** member function taking no parameters and returning a
    value. */
struct malformed_getter {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: a [[=welder::getter]] must mark a CONST member function with "
        "no parameters and a non-void return - a property read takes nothing "
        "and must not mutate; move side effects to a method, or drop the mark";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) when a
    `[[=welder::setter]]` sits on a function that is not a setter shape: it must
    be a member function taking exactly one parameter. */
struct malformed_setter {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: a [[=welder::setter]] must mark a member function taking "
        "exactly ONE parameter (the new value); a multi-argument update is a "
        "method, not a property write";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) when one function
    carries both a `getter` and a `setter` mark for the same language — a
    function supplies one property half, never both. */
struct accessor_role_conflict {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: this function carries BOTH a getter and a setter mark for one "
        "language; a function supplies one property half - split the accessors, "
        "or scope the marks to different languages";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) when two getters
    (or two setters) resolve to the same property name for one language — a
    property has exactly one function per half. */
struct duplicate_property_accessor {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: two getters (or two setters) resolve to the SAME property "
        "name for one language; a property has one function per half - "
        "mark::exclude one, scope the marks by language, or rename one with an "
        "explicit getter(\"name\")/setter(\"name\")";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) when a
    participating setter's property has no participating getter in that
    language — welder binds no write-only properties. */
struct setter_without_getter {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: this setter's property has NO participating getter for one "
        "language (a write-only property is not idiomatic anywhere welder "
        "binds); mark the matching getter, make the names pair (an explicit "
        "getter(\"name\")/setter(\"name\") always pairs), or exclude the "
        "setter for that language too";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) when an
    accessor-marked function also carries a `weld_as` — the accessor's explicit
    name is the property's rename tool, so the two would fight. */
struct accessor_weld_as_conflict {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: a getter/setter-marked function also carries weld_as, but an "
        "accessor's target name is the PROPERTY name - spell it in the mark "
        "instead: [[=welder::getter(\"name\")]]";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) on an
    accessor-marked **static** member function: static properties are not
    supported yet (deferred — the Python frameworks have `def_property_static`,
    the Lua rods have no equivalent surface). */
struct static_property_accessor {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: getter/setter marks on a STATIC member function are not "
        "supported yet; bind it as a static method (drop the mark), or expose "
        "a namespace variable instead";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) on an
    accessor-marked **virtual** member function: both Python rods dispatch
    overrides through a by-name attribute lookup, and a property object under
    that name would silently break the override protocol — rejected rather than
    bound subtly wrong. */
struct virtual_property_accessor {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: getter/setter marks on a VIRTUAL member function are not "
        "supported (a property under the method's name breaks Python override "
        "dispatch, which looks overrides up by name); bind it as a method "
        "(drop the mark), or wrap the virtual in non-virtual accessors";
};

/** Thrown by `property_entries` (`<welder/bind_traits.hpp>`) when a resolved
    property name collides with a bound data member or a (non-accessor) method
    of the same class surface. Names are compared by their case-normalized word
    sequence — convention-insensitively — because a property and a member
    differing only in spelling convention collide under any styled binding. */
struct property_name_collision {
    /** What went wrong and how to fix it. */
    const char* what =
        "welder: a property's name collides with a bound data member or "
        "method of the same class (compared word-wise, so spelling-convention "
        "variants count); rename the property with an explicit "
        "getter(\"name\"), or exclude the colliding member";
};

} // namespace welder::inline v0::diag