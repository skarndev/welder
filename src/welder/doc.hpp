#pragma once
#include <array>
#include <concepts>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/** @file
    Language-agnostic documentation layer: read `[[=welder::doc(...)]]`
    annotations off reflected entities and assemble them into a docstring under a
    pluggable *style*. Backends consume `doc_of` / `function_docstring` and never
    re-derive the annotation semantics — a new backend reuses this, picking
    whichever style fits its target language.

    @note Like `<welder/reflect.hpp>`, this depends on the welder vocabulary
    (`welder::doc_spec`, `fixed_string`) but deliberately does NOT include
    `<welder/annotations.hpp>`. Provide the vocabulary first — `#include
    <welder/vocabulary.hpp>` — then this header.
*/

namespace welder {

/** Normalize a docstring the way Python's `inspect.cleandoc` (PEP 257) does, so a
    multiline `doc`/`returns` can be indented to line up with the surrounding C++
    source without that indentation reaching the target language's docstring.

    Three steps, matching the Python convention users of the bindings expect:
    - strip leading whitespace from the *first* line (it typically abuts the
      opening `R"(` or the annotation call);
    - remove the whitespace **common** to every subsequent non-blank line (relative
      indentation — an indented example block — is preserved);
    - drop leading and trailing blank lines.

    A single-line docstring is returned unchanged. Tabs are treated as single
    characters (not expanded to tab stops): indent doc text with spaces, as C++
    source is indented generally.

    @param text the raw docstring text.
    @return the cleaned text (may be empty if the input was all whitespace).
*/
consteval std::string cleandoc(std::string_view text) {
    std::vector<std::string> lines{};
    std::string cur{};
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else
            cur.push_back(c);
    }
    lines.push_back(cur);

    auto leading = [](const std::string& s) {
        std::string::size_type n{0};
        while (n < s.size() && (s[n] == ' ' || s[n] == '\t'))
            ++n;
        return n;
    };
    auto blank = [](const std::string& s) {
        for (char c : s)
            if (c != ' ' && c != '\t' && c != '\r')
                return false;
        return true;
    };

    // Common leading whitespace of the lines *after* the first (blank lines don't
    // constrain it), which is what gets removed from those lines.
    bool have_margin{false};
    std::string::size_type margin{0};
    for (std::string::size_type i{1}; i < lines.size(); ++i)
        if (!blank(lines[i])) {
            const auto ind{leading(lines[i])};
            if (!have_margin || ind < margin) {
                margin = ind;
                have_margin = true;
            }
        }

    if (!lines.empty())
        lines[0].erase(0, leading(lines[0]));
    if (have_margin)
        for (std::string::size_type i{1}; i < lines.size(); ++i)
            lines[i].erase(0, margin); // over-erasing a short blank line is fine

    while (!lines.empty() && blank(lines.back()))
        lines.pop_back();
    while (!lines.empty() && blank(lines.front()))
        lines.erase(lines.begin());

    std::string out{};
    for (std::string::size_type i{0}; i < lines.size(); ++i) {
        if (i)
            out.push_back('\n');
        out += lines[i];
    }
    return out;
}

/** The text of the annotation on @a Ent whose class template is @a SpecTmpl, or
    `nullptr` if it carries none.

    @a Ent / @a SpecTmpl are template parameters rather than runtime arguments
    because matching requires splicing the annotation's concrete `SpecTmpl<N>`
    type, which must be a constant.

    @tparam Ent      a reflection of the entity to read.
    @tparam SpecTmpl a class template whose specialization has a `.text` member
                     convertible to a C string (`doc_spec`, `return_doc_spec`).
    @return the annotation text, @ref cleandoc "dedented" and with static storage
            (`define_static_string`, so usable at runtime), or `nullptr`.
*/
template <std::meta::info Ent, std::meta::info SpecTmpl>
consteval const char* annotation_text_of() {
    template for (constexpr auto a :
                  std::define_static_array(std::meta::annotations_of(Ent))) {
        constexpr std::meta::info t{std::meta::type_of(a)};
        if constexpr (std::meta::has_template_arguments(t) &&
                      std::meta::template_of(t) == SpecTmpl) {
            using spec_type = [:t:];
            constexpr auto spec{std::meta::extract<spec_type>(a)};
            return std::define_static_string(cleandoc(spec.text.data));
        }
    }
    return nullptr;
}

/** The `doc` text on @a Ent (a class, namespace, function, or parameter), or
    `nullptr`. A function's own `doc` is its summary.

    @tparam Ent a reflection of the documented entity.
*/
template <std::meta::info Ent>
consteval const char* doc_of() {
    return annotation_text_of<Ent, ^^doc_spec>();
}

/** The `returns` text on function @a Fn (documentation of its return value), or
    `nullptr`. A distinct spec type, so it does not collide with the summary `doc`.

    @tparam Fn a reflection of the function.
*/
template <std::meta::info Fn>
consteval const char* return_doc_of() {
    return annotation_text_of<Fn, ^^return_doc_spec>();
}

/** One documented template parameter of an entity: the parameter's name (as
    spelled in the `tparam` annotation) and its doc text. Both have static storage. */
struct tparam_doc {
    const char* name{nullptr}; /**< The template parameter's name. */
    const char* text{nullptr}; /**< Its documentation. */
};

/** How many `tparam` annotations @a Ent carries.

    They are repeatable and keep declaration order, unlike the single-shot
    doc/returns specs.
    @tparam Ent a reflection of the entity.
    @return the count of `tparam` annotations.
*/
template <std::meta::info Ent>
consteval decltype(sizeof(0)) tparam_count() {
    decltype(sizeof(0)) n{0};
    for (auto a : std::meta::annotations_of(Ent)) {
        auto t{std::meta::type_of(a)};
        if (std::meta::has_template_arguments(t) &&
            std::meta::template_of(t) == ^^tparam_spec)
            ++n;
    }
    return n;
}

/** The template-parameter docs declared on @a Ent via
    `[[=welder::tparam("T","…")]]`, in annotation order.

    @note P2996 refuses `annotations_of` on an *uninstantiated* template, so on
    gcc-16 these read off a concrete entity — in practice an instantiation, which
    inherits the governing declaration's annotations (see
    `tests/core/template_annotations.cpp`). The Doxygen filter reads the same
    annotations textually, so the C++ docs need no instantiation.

    @tparam Ent a reflection of the entity.
    @return an array of tparam_doc, one per `tparam` annotation, in order.
*/
template <std::meta::info Ent>
consteval auto tparam_docs() {
    std::array<tparam_doc, tparam_count<Ent>()> out{};
    decltype(sizeof(0)) i{0};
    template for (constexpr auto a :
                  std::define_static_array(std::meta::annotations_of(Ent))) {
        constexpr std::meta::info t{std::meta::type_of(a)};
        if constexpr (std::meta::has_template_arguments(t) &&
                      std::meta::template_of(t) == ^^tparam_spec) {
            using spec_type = [:t:];
            constexpr auto spec{std::meta::extract<spec_type>(a)};
            out[i++] = tparam_doc{std::define_static_string(spec.name.data),
                                  std::define_static_string(spec.text.data)};
        }
    }
    return out;
}

/** One function parameter's documentation: its identifier (`nullptr` if unnamed)
    and its `doc` text (`nullptr` if undocumented). */
struct param_doc {
    const char* name{nullptr}; /**< The parameter's identifier, or `nullptr`. */
    const char* text{nullptr}; /**< Its `doc` text, or `nullptr`. */
};

/** The parameter docs of function @a Fn, in declaration order.

    Names and texts use static storage, so the array (and spans over it) stay valid
    at runtime.
    @tparam Fn a reflection of the function.
    @return an array of param_doc, one per parameter, in declaration order.
*/
template <std::meta::info Fn>
consteval auto param_docs() {
    constexpr decltype(sizeof(0)) n{std::meta::parameters_of(Fn).size()};
    std::array<param_doc, n> out{};
    decltype(sizeof(0)) i{0};
    template for (constexpr auto p :
                  std::define_static_array(std::meta::parameters_of(Fn))) {
        const char* name{std::meta::has_identifier(p)
                             ? std::define_static_string(std::meta::identifier_of(p))
                             : nullptr};
        out[i++] = param_doc{name, doc_of<p>()};
    }
    return out;
}

// --- docstring styles -------------------------------------------------------

/** The raw documentation pieces of a function, handed to a style to assemble.

    A struct (rather than a growing argument list) so adding future sections — a
    `Raises:`, a `Note:` — does not re-break the @ref doc_style concept. Any member may
    be empty/null: a function with only a `returns` and no summary is valid.
*/
struct function_doc {
    const char* summary{nullptr};        /**< The function's own `doc`. */
    std::span<const param_doc> params{}; /**< Per-parameter `doc`, declaration order. */
    const char* returns{nullptr};        /**< The function's `returns`. */
};

/** A *style* folds a function_doc into one docstring.

    It is the customization point for how documentation reads in the target
    language; swap it to emit Google-, NumPy-, or any house style. Any type with
    `static std::string format(const function_doc&)` qualifies. Concrete styles
    live with the rods that share them (e.g. `welder::rods::python::google_style`
    in `<welder/rods/python/doc_style.hpp>`), keeping this core layer neutral.

    @tparam S the candidate style type.
*/
template <class S>
concept doc_style = requires(const function_doc& d) {
    { S::format(d) } -> std::same_as<std::string>;
};

/** The complete docstring for function @a Fn under @a Style.

    Its own `doc` summary with its parameter docs and `returns` folded in. Empty
    when the function carries no documentation at all, so a backend can skip
    emitting it.

    @tparam Fn    a reflection of the function.
    @tparam Style the docstring style (a @ref doc_style); the caller picks one that
                  fits its target language.
    @return the assembled docstring, or empty if undocumented.
*/
template <std::meta::info Fn, doc_style Style>
std::string function_docstring() {
    static constexpr auto pds{param_docs<Fn>()};
    return Style::format(function_doc{doc_of<Fn>(),
                                      std::span<const param_doc>{pds},
                                      return_doc_of<Fn>()});
}

} // namespace welder
