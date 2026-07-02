#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <utility>

#include <welder/doc.hpp>  // doc_of / function_docstring / doc_style
#include <welder/docs.hpp> // the doc walker + doc_emitter contract

// Doxygen emitter for the C++ docs pipeline: generates a **shadow header** — a
// plain C++ declaration tree whose welder `doc`/`returns` annotations have become
// ordinary Doxygen comments (`/** */`, `@param`, `@return`, `///<`).
//
// Why a header and not Doxygen XML: XML is Doxygen's *output* format and cannot
// be fed back as input, whereas a shadow header drops into any existing Doxygen
// project — point the Doxyfile's INPUT at the generated tree alongside the
// hand-written pages (@mainpage/@page/@defgroup), exclude the real sources, and
// @ref cross-links resolve as if the API had been hand-commented. The output is
// deliberately boring C++, so the doc build never depends on Doxygen coping with
// C++26 in the real sources. It is also a human-readable, diffable artifact.
//
// Like every reflection-layer header, this expects the welder vocabulary first
// (`import welder;` or `#include <welder/welder.hpp>`), then this header.

namespace welder::docs {

namespace detail {

// The display name of a type reflection, in static storage. Display strings are
// the compiler's rendering (qualified names, e.g. `atelier::Extent`) — stable
// per toolchain, which is all a generated artifact needs.
consteval const char* type_name(std::meta::info type) {
    return std::define_static_string(std::meta::display_string_of(type));
}

// A member operator's C++ name: "operator" + its token (`operator+`,
// `operator[]`, ...). Unlike a binding backend there is no name *mapping* to a
// target language — C++ docs show the operator as itself.
template <std::meta::info Fn>
consteval const char* operator_name() {
    std::string n{"operator"};
    n += std::meta::symbol_of(std::meta::operator_of(Fn));
    return std::define_static_string(n);
}

// Fn's parameter list as declaration text: "int value, int factor" (a parameter
// keeps its name when it has one — that is what @param references).
template <std::meta::info Fn>
consteval const char* param_list() {
    std::string s{};
    for (auto p : std::meta::parameters_of(Fn)) {
        if (!s.empty())
            s += ", ";
        s += std::meta::display_string_of(std::meta::type_of(p));
        if (std::meta::has_identifier(p)) {
            s += ' ';
            s += std::meta::identifier_of(p);
        }
    }
    return std::define_static_string(s);
}

// The public-base heads of Class: " : public A, public B", or "" for none.
// C++ docs show real inheritance (nothing is flattened), so the bases appear on
// the class head exactly as declared.
template <std::meta::info Class>
consteval const char* base_list() {
    std::string s{};
    for (auto b : welder::public_bases(Class)) {
        s += s.empty() ? " : public " : ", public ";
        s += std::meta::display_string_of(b);
    }
    return std::define_static_string(s);
}

} // namespace detail

// Doxygen flavor of the core doc_style: the summary, then one `@param name text`
// per *documented* parameter, then `@return text` — one line each, no wrapping
// (the emitter frames the lines into a comment block).
struct doxygen_style {
    static std::string format(const function_doc& d) {
        std::string out{};
        auto line = [&out](const char* text) {
            if (!out.empty())
                out += '\n';
            out += text;
        };
        if (d.summary)
            out += d.summary;
        for (const auto& p : d.params)
            if (p.text) {
                line("@param ");
                out += p.name ? p.name : "?";
                out += ' ';
                out += p.text;
            }
        if (d.returns) {
            line("@return ");
            out += d.returns;
        }
        return out;
    }
};
static_assert(doc_style<doxygen_style>);

// The doc_emitter producing the shadow header. Stateful by design: it
// accumulates the whole header into `out` as the walker visits entities.
struct doxygen_emitter {
    std::string out{};

    // --- comment framing ------------------------------------------------------
    void pad(int n) { out.append(static_cast<std::size_t>(n), ' '); }

    // One-line /** ... */ above a declaration (classes, fields, variables).
    void brief(const char* text, int ind) {
        if (!text)
            return;
        pad(ind);
        out += "/** ";
        out += text;
        out += " */\n";
    }

    // Comment block for a function's folded doc (summary/@param/@return).
    // A one-line body (e.g. a bare summary) collapses to the brief form.
    void block(const std::string& body, int ind) {
        if (body.empty())
            return;
        if (body.find('\n') == std::string::npos) {
            brief(body.c_str(), ind);
            return;
        }
        pad(ind);
        out += "/**\n";
        for (std::size_t pos{0};;) {
            const std::size_t nl{body.find('\n', pos)};
            pad(ind);
            out += " * ";
            out.append(body, pos, nl == std::string::npos ? nl : nl - pos);
            out += '\n';
            if (nl == std::string::npos)
                break;
            pos = nl + 1;
        }
        pad(ind);
        out += " */\n";
    }

    // A function declaration line: doc block, then
    // "[static ]ret name(params)[ const];" at the given indent.
    template <std::meta::info Fn>
    void declare_function(const char* name, int ind) {
        block(function_docstring<Fn, doxygen_style>(), ind);
        pad(ind);
        if constexpr (std::meta::is_static_member(Fn))
            out += "static ";
        out += detail::type_name(std::meta::return_type_of(Fn));
        out += ' ';
        out += name;
        out += '(';
        out += detail::param_list<Fn>();
        out += ')';
        if constexpr (std::meta::is_const(Fn))
            out += " const";
        out += ";\n";
    }

    // --- namespaces -----------------------------------------------------------
    void open_namespace(const char* name, const char* doc) {
        out += '\n';
        brief(doc, 0);
        out += "namespace ";
        out += name;
        out += " {\n";
    }
    void close_namespace(const char* name) {
        out += "\n} // namespace ";
        out += name;
        out += '\n';
    }

    // --- classes ----------------------------------------------------------------
    template <std::meta::info Class>
    void open_class() {
        out += '\n';
        brief(welder::doc_of<Class>(), 0);
        out += "struct ";
        out += std::define_static_string(std::meta::identifier_of(Class));
        out += detail::base_list<Class>();
        out += " {\n";
    }
    template <std::meta::info Class>
    void close_class() {
        out += "};\n";
    }

    template <std::meta::info Class>
    void emit_default_ctor() {
        pad(4);
        out += std::define_static_string(std::meta::identifier_of(Class));
        out += "();\n";
    }

    template <std::meta::info Ctor>
    void emit_constructor() {
        block(function_docstring<Ctor, doxygen_style>(), 4);
        pad(4);
        out += std::define_static_string(
            std::meta::identifier_of(std::meta::parent_of(Ctor)));
        out += '(';
        out += detail::param_list<Ctor>();
        out += ");\n";
    }

    template <std::meta::info Mem>
    void emit_field() {
        brief(welder::doc_of<Mem>(), 4);
        pad(4);
        out += detail::type_name(std::meta::type_of(Mem));
        out += ' ';
        out += std::define_static_string(std::meta::identifier_of(Mem));
        out += ";\n";
    }

    template <std::meta::info Fn>
    void emit_method() {
        declare_function<Fn>(
            std::define_static_string(std::meta::identifier_of(Fn)), 4);
    }

    template <std::meta::info Fn>
    void emit_operator() {
        declare_function<Fn>(detail::operator_name<Fn>(), 4);
    }

    // --- enums ------------------------------------------------------------------
    template <std::meta::info Enum>
    void open_enum() {
        out += '\n';
        brief(welder::doc_of<Enum>(), 0);
        out += "enum ";
        if constexpr (std::meta::is_scoped_enum_type(Enum))
            out += "class ";
        out += std::define_static_string(std::meta::identifier_of(Enum));
        out += " {\n";
    }

    // Per-enumerator docs live here as `///<` trailing comments — the C++
    // reference shows what a binding backend may have to drop (pybind11's
    // .value() takes no docstring).
    template <std::meta::info En>
    void emit_enumerator() {
        pad(4);
        out += std::define_static_string(std::meta::identifier_of(En));
        out += ',';
        if (const char* d{welder::doc_of<En>()}) {
            out += " ///< ";
            out += d;
        }
        out += '\n';
    }

    template <std::meta::info Enum>
    void close_enum() {
        out += "};\n";
    }

    // --- namespace members --------------------------------------------------------
    template <std::meta::info Fn>
    void emit_function() {
        out += '\n';
        declare_function<Fn>(
            std::define_static_string(std::meta::identifier_of(Fn)), 0);
    }

    // A namespace variable declares as `extern` (const stays in its type): the
    // shadow header documents, it never defines. Variable docs — dropped by the
    // binding backends (no attribute __doc__) — surface here.
    template <std::meta::info Var>
    void emit_variable() {
        out += '\n';
        brief(welder::doc_of<Var>(), 0);
        out += "extern ";
        out += detail::type_name(std::meta::type_of(Var));
        out += ' ';
        out += std::define_static_string(std::meta::identifier_of(Var));
        out += ";\n";
    }
};
static_assert(doc_emitter<doxygen_emitter>);

// The complete shadow header for namespace Ns, as a string: preamble, then the
// declaration tree with the welder docs as Doxygen comments. Write it to a file
// and hand that file (not the real sources) to a Doxyfile's INPUT.
template <std::meta::info Ns>
std::string shadow_header() {
    doxygen_emitter e{};
    e.out += "// Generated by welder — C++ API reference (shadow header). "
             "Do not edit.\n"
             "#pragma once\n";
    document_namespace<Ns>(e);
    return std::move(e.out);
}

} // namespace welder::docs
