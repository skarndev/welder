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

// The text of the annotation on `Ent` whose class template is `SpecTmpl`, or
// nullptr if it carries none. `Ent`/`SpecTmpl` are template parameters rather than
// runtime arguments because matching requires splicing the annotation's concrete
// `SpecTmpl<N>` type, which must be a constant. `SpecTmpl` must be a class template
// whose specialization has a `.text` member convertible to a C string (`doc_spec`,
// `return_doc_spec`). The returned pointer has static storage
// (define_static_string), so it is usable at runtime.
template <std::meta::info Ent, std::meta::info SpecTmpl>
consteval const char* annotation_text_of() {
    template for (constexpr auto a :
                  std::define_static_array(std::meta::annotations_of(Ent))) {
        constexpr std::meta::info t{std::meta::type_of(a)};
        if constexpr (std::meta::has_template_arguments(t) &&
                      std::meta::template_of(t) == SpecTmpl) {
            using spec_type = [:t:];
            constexpr auto spec{std::meta::extract<spec_type>(a)};
            return std::define_static_string(spec.text.data);
        }
    }
    return nullptr;
}

// The `doc` text on `Ent` (a class, namespace, function, or parameter), or
// nullptr. The function's own `doc` is its summary.
template <std::meta::info Ent>
consteval const char* doc_of() {
    return annotation_text_of<Ent, ^^doc_spec>();
}

// The `returns` text on function `Fn` (documentation of its return value), or
// nullptr. A distinct spec type, so it does not collide with the summary `doc`.
template <std::meta::info Fn>
consteval const char* return_doc_of() {
    return annotation_text_of<Fn, ^^return_doc_spec>();
}

// One documented template parameter of an entity: the parameter's name (as
// spelled in the tparam annotation) and its doc text. Both have static storage.
struct tparam_doc {
    const char* name{nullptr};
    const char* text{nullptr};
};

// How many `tparam` annotations `Ent` carries (they are repeatable and keep
// declaration order, unlike the single-shot doc/returns specs).
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

// The template-parameter docs declared on `Ent` via [[=welder::tparam("T","…")]],
// in annotation order. NB P2996 refuses annotations_of on an *uninstantiated*
// template, so on gcc-16 these read off a concrete entity — in practice an
// instantiation, which inherits the governing declaration's annotations (see
// tests/core/template_annotations.cpp). The Doxygen filter reads the same
// annotations textually, so the C++ docs need no instantiation.
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
// The raw documentation pieces of a function, handed to a style to assemble. A
// struct (rather than a growing argument list) so adding future sections — a
// `Raises:`, a `Note:` — does not re-break the `doc_style` concept. Any member
// may be empty/null: a function with only a `returns` and no summary is valid.
struct function_doc {
    const char* summary{nullptr};       // the function's own `doc`
    std::span<const param_doc> params{}; // per-parameter `doc`, declaration order
    const char* returns{nullptr};       // the function's `returns`
};

// A *style* folds a `function_doc` into one docstring. It is the customization
// point for how documentation reads in the target language; swap it to emit
// Google-, NumPy-, or any house style. Any type with
// `static std::string format(const function_doc&)` qualifies.

template <class S>
concept doc_style = requires(const function_doc& d) {
    { S::format(d) } -> std::same_as<std::string>;
};

// Google-style: the summary, then an `Args:` block listing each *documented*
// parameter ("    name: text"), then a `Returns:` block. Undocumented parameters
// are omitted; the `Args:`/`Returns:` blocks are dropped entirely when empty,
// and blocks are separated from preceding content by a blank line.
struct google_style {
    static std::string format(const function_doc& d) {
        std::string out{};
        // Separate a new block from preceding content by exactly one blank line,
        // whether or not that content already ended in a newline (the Args block
        // leaves a trailing one per parameter line; the summary does not).
        auto blank_line = [&out]() {
            if (!out.empty()) {
                if (out.back() != '\n')
                    out += '\n';
                out += '\n';
            }
        };

        if (d.summary)
            out += d.summary;

        bool any_param_doc{false};
        for (const auto& p : d.params)
            if (p.text) {
                any_param_doc = true;
                break;
            }
        if (any_param_doc) {
            blank_line();
            out += "Args:\n";
            for (const auto& p : d.params)
                if (p.text) {
                    out += "    ";
                    out += p.name ? p.name : "?";
                    out += ": ";
                    out += p.text;
                    out += '\n';
                }
        }

        if (d.returns) {
            blank_line();
            out += "Returns:\n    ";
            out += d.returns;
        }
        return out;
    }
};

// The complete docstring for function `Fn` under `Style` (Google by default): its
// own `doc` summary with its parameter docs and `returns` folded in. Empty when
// the function carries no documentation at all, so a backend can skip emitting it.
template <std::meta::info Fn, doc_style Style = google_style>
std::string function_docstring() {
    static constexpr auto pds{param_docs<Fn>()};
    return Style::format(function_doc{doc_of<Fn>(),
                                      std::span<const param_doc>{pds},
                                      return_doc_of<Fn>()});
}

} // namespace welder
