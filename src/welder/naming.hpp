#pragma once
#include <meta>
#include <stdexcept> // name_of_or: missing-override diagnostic
#include <string>
#include <string_view>
#include <vector>

#include <welder/concepts.hpp> // the name_style concept (welder::naming::name_style)

/** @file
    Language-agnostic **name styling**: reshape a C++ identifier into a target
    language's naming convention, and resolve the final bound name of an entity
    (honouring a verbatim `[[=welder::weld_as]]` override first).

    C++ code follows one house style (`processFile`, `HTTPServer`, `max_count`);
    the language it is bound to usually wants another (PEP 8 asks for `process_file`
    and `MaxCount`). A **name style** is the pluggable customization point that does
    that reshaping — you hand one to `welder::welder<Rod, Style>` and every generated
    name flows through it.

    The style is asked to name each entity through a **per-kind hook** —
    `transform_class`, `transform_method`, `transform_field`, `transform_enumerator`,
    … — because the driver already knows *what* it is binding, and the same
    identifier is styled differently by kind (PEP 8 PascalCases a class but
    snake_cases a method). A style therefore never has to inspect the reflection to
    discover the kind; it just implements the hooks it wants to reshape and inherits
    the rest.

    The hard part each hook faces is that *the input format is unknown*: an
    identifier may arrive in `snake_case`, `camelCase`, `PascalCase` or
    `SCREAMING_CASE`. So the shared machinery here first **splits** an identifier
    into its constituent words (on underscores/hyphens, camel-case humps and acronym
    boundaries), then **re-joins** them in the requested convention — a round-trip
    stable regardless of the source spelling. @ref welder::naming::none is the
    identity (bind the C++ identifier unchanged, the default); the single-convention
    styles (`snake_case`, `pascal_case`, …) apply one convention to every kind; a
    convention *mix* that depends on the kind (PEP 8) is built per language beside
    the rods (e.g. `welder::rods::python::pep8`).

    @note Like `<welder/reflect.hpp>`/`<welder/doc.hpp>`, this depends on the welder
    vocabulary (`welder::detail::weld_as_spec`, `lang`, `lang_bit`) but deliberately does NOT
    include `<welder/annotations.hpp>`: the vocabulary may instead arrive via `import
    welder;`. Provide the vocabulary first, then this header.
*/

namespace welder::inline v0::naming {

// --- identifier word-splitting ----------------------------------------------

/** Split an identifier into lower-cased words, however it was spelled.

    Word boundaries are taken at: an underscore, hyphen or space (each a separator,
    consumed); a lower/digit → upper transition (`fooBar` → `foo`, `bar`); and the
    end of an acronym run, i.e. an upper followed by an upper-then-lower
    (`HTTPServer` → `http`, `server`). Digits stay attached to the preceding word
    (`vec3` → `vec3`). Every emitted word is lower-cased, so a joiner controls the
    output casing on its own.

    @param id the source identifier.
    @return the identifier's words, lower-cased, in order (empty for an all-separator
            or empty input).
*/
consteval std::vector<std::string> split_words(std::string_view id) {
    auto is_upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    auto is_lower = [](char c) { return c >= 'a' && c <= 'z'; };
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    auto to_lower = [&](char c) { return is_upper(c) ? char(c - 'A' + 'a') : c; };

    std::vector<std::string> words{};
    std::string cur{};
    auto flush = [&] {
        if (!cur.empty()) {
            words.push_back(cur);
            cur.clear();
        }
    };
    for (std::string::size_type i{0}; i < id.size(); ++i) {
        const char c{id[i]};
        if (c == '_' || c == '-' || c == ' ') {
            flush();
            continue;
        }
        if (!cur.empty()) {
            const char prev{id[i - 1]};
            const bool hump{is_upper(c) && (is_lower(prev) || is_digit(prev))};
            const bool acronym_end{is_upper(c) && is_upper(prev) &&
                                   i + 1 < id.size() && is_lower(id[i + 1])};
            if (hump || acronym_end)
                flush();
        }
        cur.push_back(to_lower(c));
    }
    flush();
    return words;
}

// --- word joiners (the common conventions) ----------------------------------

/** The naming convention a joiner emits. */
enum class case_kind {
    snake,           /**< `foo_bar` */
    screaming_snake, /**< `FOO_BAR` */
    kebab,           /**< `foo-bar` */
    camel,           /**< `fooBar` */
    pascal,          /**< `FooBar` */
};

/** Join already-lower-cased @a words in convention @a kind.
    @param words the lower-cased words (as from @ref split_words).
    @param kind  the convention to render.
    @return the joined identifier body. */
consteval std::string join_words(const std::vector<std::string>& words,
                                 case_kind kind) {
    auto upper = [](char c) { return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c; };
    auto cap = [&](std::string w) {
        if (!w.empty())
            w[0] = upper(w[0]);
        return w;
    };
    std::string out{};
    for (std::string::size_type i{0}; i < words.size(); ++i) {
        switch (kind) {
        case case_kind::snake:
            if (i)
                out += '_';
            out += words[i];
            break;
        case case_kind::screaming_snake:
            if (i)
                out += '_';
            for (char c : words[i])
                out += upper(c);
            break;
        case case_kind::kebab:
            if (i)
                out += '-';
            out += words[i];
            break;
        case case_kind::camel:
            out += i ? cap(words[i]) : words[i];
            break;
        case case_kind::pascal:
            out += cap(words[i]);
            break;
        }
    }
    return out;
}

/** Re-spell identifier @a id in convention @a kind, preserving any leading and
    trailing underscore run (`_private` / `type_` keep their fixup underscores).

    An identifier that is empty or all underscores is returned unchanged.
    @param id   the source identifier (any spelling).
    @param kind the target convention.
    @return the re-spelled identifier. */
consteval std::string restyle(std::string_view id, case_kind kind) {
    std::string::size_type lead{0};
    while (lead < id.size() && id[lead] == '_')
        ++lead;
    if (lead == id.size()) // empty or all underscores: nothing to restyle
        return std::string{id};
    std::string::size_type trail{0};
    while (id[id.size() - 1 - trail] == '_')
        ++trail;
    return std::string(lead, '_') + join_words(split_words(id), kind) +
           std::string(trail, '_');
}

// --- the style customization point ------------------------------------------
//
// The `name_style` concept — the per-kind hook contract a style below implements —
// lives in <welder/concepts.hpp> (with welder's other interface concepts).

/** The identity style: bind every C++ identifier unchanged. The default for
    `welder::welder<Rod, Style>`, and the base a custom style inherits so it need
    only override the kinds it reshapes. */
struct none {
    static consteval std::string transform_class(std::meta::info e) { return id(e); }
    static consteval std::string transform_enum(std::meta::info e) { return id(e); }
    static consteval std::string transform_enumerator(std::meta::info e) { return id(e); }
    static consteval std::string transform_method(std::meta::info e) { return id(e); }
    static consteval std::string transform_static_method(std::meta::info e) { return id(e); }
    static consteval std::string transform_function(std::meta::info e) { return id(e); }
    static consteval std::string transform_field(std::meta::info e) { return id(e); }
    static consteval std::string transform_variable(std::meta::info e) { return id(e); }
    static consteval std::string transform_submodule(std::meta::info e) { return id(e); }

  private:
    static consteval std::string id(std::meta::info e) {
        return std::string{std::meta::identifier_of(e)};
    }
};
static_assert(name_style<none>, "welder: naming::none is not a name_style");

/** A single-convention style: reshape *every* kind to convention @a Kind, whatever
    the source spelling. The base behind the named aliases below, and a handy base
    for a language style that reshapes most kinds one way (override the exceptions).
    @tparam Kind the convention every name is rendered in. */
template <case_kind Kind>
struct uniform {
    static consteval std::string transform_class(std::meta::info e) { return in(e); }
    static consteval std::string transform_enum(std::meta::info e) { return in(e); }
    static consteval std::string transform_enumerator(std::meta::info e) { return in(e); }
    static consteval std::string transform_method(std::meta::info e) { return in(e); }
    static consteval std::string transform_static_method(std::meta::info e) { return in(e); }
    static consteval std::string transform_function(std::meta::info e) { return in(e); }
    static consteval std::string transform_field(std::meta::info e) { return in(e); }
    static consteval std::string transform_variable(std::meta::info e) { return in(e); }
    static consteval std::string transform_submodule(std::meta::info e) { return in(e); }

  private:
    static consteval std::string in(std::meta::info e) {
        return restyle(std::meta::identifier_of(e), Kind);
    }
};

// Each predefined style is checked against the `name_style` concept right at its
// definition, so a hook that drifts out of contract (a missing or wrongly-typed
// transform_*) fails loudly here rather than at some rod's call site.
using snake_case = uniform<case_kind::snake>; /**< `foo_bar` everywhere */
static_assert(name_style<snake_case>, "welder: naming::snake_case is not a name_style");
using screaming_snake_case = uniform<case_kind::screaming_snake>; /**< `FOO_BAR` everywhere */
static_assert(name_style<screaming_snake_case>,
              "welder: naming::screaming_snake_case is not a name_style");
using kebab_case = uniform<case_kind::kebab>; /**< `foo-bar` everywhere */
static_assert(name_style<kebab_case>, "welder: naming::kebab_case is not a name_style");
using camel_case = uniform<case_kind::camel>; /**< `fooBar` everywhere */
static_assert(name_style<camel_case>, "welder: naming::camel_case is not a name_style");
using pascal_case = uniform<case_kind::pascal>; /**< `FooBar` everywhere */
static_assert(name_style<pascal_case>, "welder: naming::pascal_case is not a name_style");

} // namespace welder::naming

namespace welder::inline v0 {

/** The nameable entity kinds welder distinguishes, one per name-style hook —
    picked by the driver/rod so @ref name_of calls the right transform. */
enum class ent_kind {
    class_,        /**< a class/struct type → `transform_class`. */
    enum_,         /**< an enum type → `transform_enum`. */
    enumerator,    /**< an enumerator → `transform_enumerator`. */
    method,        /**< a member function → `transform_method`. */
    static_method, /**< a static member function → `transform_static_method`. */
    function,      /**< a free function → `transform_function`. */
    field,         /**< a data member → `transform_field`. */
    variable,      /**< a namespace variable → `transform_variable`. */
    submodule,     /**< a namespace bound as a submodule → `transform_submodule`. */
};

/** The verbatim `weld_as` name forced on @a Ent for language @a L, or `nullptr`.

    Scans @a Ent's annotations for a @ref detail::weld_as_spec covering @a L (a mask of `0`
    covers all languages); the first match wins. The returned string has static
    storage (`define_static_string`), so it is usable at runtime.

    @tparam Ent a reflection of the entity to read.
    @tparam L   the target language.
    @return the forced name, or `nullptr` if @a Ent carries no `weld_as` for @a L.
*/
template <std::meta::info Ent, lang L>
consteval const char* weld_as_of() {
    template for (constexpr auto a :
                  std::define_static_array(std::meta::annotations_of(Ent))) {
        constexpr std::meta::info t{std::meta::type_of(a)};
        if constexpr (std::meta::has_template_arguments(t) &&
                      std::meta::template_of(t) == ^^detail::weld_as_spec) {
            using spec_type = [:t:];
            constexpr auto spec{std::meta::extract<spec_type>(a)};
            if constexpr (spec.mask == 0 || (spec.mask & lang_bit(L)) != 0)
                return std::define_static_string(spec.name.data);
        }
    }
    return nullptr;
}

/** The final bound name of @a Ent (a @a K-kind entity) for language @a L under name
    style @a Style.

    A `[[=welder::weld_as]]` override wins and is used **verbatim** (it never flows
    through @a Style); otherwise @a Ent's identifier is reshaped by @a Style's hook
    for kind @a K. The result has static storage, so both the driver and the rods
    feed it straight to their frameworks. This is the single place a bound name is
    decided — the driver uses it for class/enum/submodule names, each rod for its
    members.

    @tparam Ent   a reflection of the entity to name.
    @tparam L     the target language.
    @tparam Style the name style (a @ref welder::naming::name_style).
    @tparam K     which of @a Style's per-kind hooks to apply.
    @return the bound name, in static storage.
*/
template <std::meta::info Ent, lang L, class Style, ent_kind K>
consteval const char* name_of() {
    if (const char* forced{weld_as_of<Ent, L>()})
        return forced;
    if constexpr (K == ent_kind::class_)
        return std::define_static_string(Style::transform_class(Ent));
    else if constexpr (K == ent_kind::enum_)
        return std::define_static_string(Style::transform_enum(Ent));
    else if constexpr (K == ent_kind::enumerator)
        return std::define_static_string(Style::transform_enumerator(Ent));
    else if constexpr (K == ent_kind::method)
        return std::define_static_string(Style::transform_method(Ent));
    else if constexpr (K == ent_kind::static_method)
        return std::define_static_string(Style::transform_static_method(Ent));
    else if constexpr (K == ent_kind::function)
        return std::define_static_string(Style::transform_function(Ent));
    else if constexpr (K == ent_kind::field)
        return std::define_static_string(Style::transform_field(Ent));
    else if constexpr (K == ent_kind::variable)
        return std::define_static_string(Style::transform_variable(Ent));
    else // ent_kind::submodule
        return std::define_static_string(Style::transform_submodule(Ent));
}

/** Resolve a bound name with a call-site override: @a override_ wins verbatim,
    `nullptr` falls back to @ref name_of.

    This is the form the driver and every rod use to honor the optional trailing
    `name` on `weld_type` / `weld_function` / `weld_variable` /
    `weld_namespace_as_submodule`. It exists because the fallback must be **lazy**:
    an entity with no identifier — a class/function *template instantiation* like
    `Box<int>` — can only be named by a `weld_as` or by the call-site override, and
    a bare `override_ ? override_ : name_of<…>()` would still constant-evaluate the
    consteval `name_of` (and hard-error on `identifier_of`) even when the override
    is present. Here the `name_of` fallback is compiled only when the entity is
    statically nameable; otherwise a missing override throws at binding time (it
    cannot be a compile-time diagnostic — whether the runtime pointer is null is
    unknowable there).

    @tparam Ent   a reflection of the entity to name.
    @tparam L     the target language.
    @tparam Style the name style (a @ref welder::naming::name_style).
    @tparam K     which of @a Style's per-kind hooks to apply.
    @param override_ the verbatim call-site name, or `nullptr` for the resolved one.
    @return the bound name, in static storage (or @a override_, whose lifetime the
            caller owns).
    @throw std::invalid_argument when the entity has neither an identifier nor a
           `weld_as` for @a L and @a override_ is `nullptr`.
*/
template <std::meta::info Ent, lang L, class Style, ent_kind K>
constexpr const char* name_of_or(const char* override_) {
    if (override_)
        return override_;
    if constexpr (weld_as_of<Ent, L>() != nullptr || std::meta::has_identifier(Ent)) {
        return name_of<Ent, L, Style, K>();
    } else {
        throw std::invalid_argument{
            "welder: this entity has no identifier (a template instantiation?) and "
            "no [[=welder::weld_as]] — pass an explicit name at the weld_* call"};
    }
}

} // namespace welder
