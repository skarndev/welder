#pragma once
#include <array>
#include <concepts>
#include <meta>
#include <span>
#include <string>

// Language-agnostic documentation layer: read [[=welder::doc(...)]] annotations
// off reflected entities and assemble them into a docstring under a pluggable
// *style*. Backends consume `doc_of` / `function_docstring` and never re-derive
// the annotation semantics — a new backend reuses this, picking whichever style
// fits its target language.
//
// Like <welder/reflect.hpp>, this depends on the welder vocabulary
// (welder::doc_spec, fixed_string) but deliberately does NOT include
// <welder/annotations.hpp>: the vocabulary may instead arrive via `import
// welder;`, and including it textually as well would redeclare those entities.
// Provide the vocabulary first — `import welder;` or `#include
// <welder/welder.hpp>` — then this header.

namespace welder {

// The docstring attached to `Ent` (a class, namespace, function, or parameter),
// or nullptr if it carries no `doc`. `Ent` is a template parameter rather than a
// runtime argument because `doc_spec` is a template: matching it requires
// splicing the annotation's concrete `doc_spec<N>` type, which must be a
// constant. The returned pointer has static storage (define_static_string), so
// it is usable at runtime.
template <std::meta::info Ent>
consteval const char* doc_of() {
    template for (constexpr auto a :
                  std::define_static_array(std::meta::annotations_of(Ent))) {
        constexpr std::meta::info t{std::meta::type_of(a)};
        if constexpr (std::meta::has_template_arguments(t) &&
                      std::meta::template_of(t) == ^^doc_spec) {
            using spec_type = [:t:];
            constexpr auto spec{std::meta::extract<spec_type>(a)};
            return std::define_static_string(spec.text.data);
        }
    }
    return nullptr;
}

// One function parameter's documentation: its identifier (nullptr if unnamed)
// and its `doc` text (nullptr if undocumented).
struct param_doc {
    const char* name{nullptr};
    const char* text{nullptr};
};

// The parameter docs of function `Fn`, in declaration order. Names and texts use
// static storage, so the array (and spans over it) stay valid at runtime.
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
//
// A *style* folds a function's own doc (its summary) plus its parameter docs
// into one docstring. It is the customization point for how documentation reads
// in the target language; swap it to emit Google-, NumPy-, or any house style.
// Any type with `static std::string format(const char* summary,
// std::span<const param_doc>)` qualifies.

template <class S>
concept doc_style =
    requires(const char* summary, std::span<const param_doc> params) {
        { S::format(summary, params) } -> std::same_as<std::string>;
    };

// Google-style: the summary, then an `Args:` block listing each *documented*
// parameter ("    name: text"). Undocumented parameters are omitted; if none are
// documented the block is dropped entirely, leaving just the summary.
struct google_style {
    static std::string format(const char* summary,
                              std::span<const param_doc> params) {
        std::string out{};
        if (summary)
            out += summary;
        bool any_documented{false};
        for (const auto& p : params)
            if (p.text) {
                any_documented = true;
                break;
            }
        if (any_documented) {
            if (!out.empty())
                out += "\n\n";
            out += "Args:\n";
            for (const auto& p : params)
                if (p.text) {
                    out += "    ";
                    out += p.name ? p.name : "?";
                    out += ": ";
                    out += p.text;
                    out += '\n';
                }
        }
        return out;
    }
};

// The complete docstring for function `Fn` under `Style` (Google by default):
// its own `doc` summary with its parameter docs folded in. Empty when the
// function carries no documentation at all, so a backend can skip emitting it.
template <std::meta::info Fn, doc_style Style = google_style>
std::string function_docstring() {
    static constexpr auto pds{param_docs<Fn>()};
    return Style::format(doc_of<Fn>(), std::span<const param_doc>{pds});
}

} // namespace welder
