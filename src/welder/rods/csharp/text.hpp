#pragma once
#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

/** @file
    The C# backend's small text utilities — the pieces of pure string handling
    that carry no reflection and no marshalling knowledge.

    They are shared by the document assembler
    (`<welder/rods/csharp/document.hpp>`) and by every emission layer under
    `<welder/rods/csharp/emit/…>`, which is why they live apart from both: a
    helper that only ever looks at characters should not drag `<meta>` in.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

/** @name The `{}` mini-format
    One formatting vocabulary for BOTH worlds this backend assembles text in:
    constant evaluation (symbol mangling, `emit/refs.hpp`'s constant-init
    spellings) and generator runtime (every emitter, via
    @ref welder::rods::csharp::code_writer). Neither `std::format` nor
    `std::to_string` is usable in constant evaluation (verified on gcc-16 /
    libstdc++), and {fmt}'s compile-time path would add a third-party dependency
    to an otherwise dependency-free rod — so welder owns these ~40 lines.
    `constexpr` (not `consteval`) is the point: the SAME implementation runs in
    both worlds, so a spelling can move between them without reformatting. @{ */

/** Append one format argument to @a out: the string-ish overload.
    @param out the buffer under construction.
    @param s   the argument (`string_view`, `const char*`, `std::string` and
               `char` all land here via the overload set). */
constexpr void cat_append(std::string& out, std::string_view s) { out += s; }
/** @copydoc cat_append(std::string&,std::string_view) */
constexpr void cat_append(std::string& out, char s) { out += s; }

/** Append one format argument to @a out: the integral overload, rendered as
    decimal (`std::to_string` is not constexpr, so the digits are hand-rolled).
    @tparam I   an integral type other than `char`/`bool`.
    @param out the buffer under construction.
    @param v   the value to render. */
template <std::integral I>
    requires(!std::same_as<I, char> && !std::same_as<I, bool>)
constexpr void cat_append(std::string& out, I v) {
    if (v == 0) {
        out += '0';
        return;
    }
    char tmp[24]{};
    std::size_t n{0};
    using U = std::make_unsigned_t<I>;
    U u{v < 0 ? static_cast<U>(0) - static_cast<U>(v) : static_cast<U>(v)};
    while (u != 0) {
        tmp[n++] = static_cast<char>('0' + u % 10);
        u /= 10;
    }
    if (v < 0)
        out += '-';
    while (n != 0)
        out += tmp[--n];
}

/** Format @a f, substituting each `{}` with the next of @a args — the
    backend's `std::format` stand-in, usable in constant evaluation and at
    runtime alike (see the group note above). Only what the emitted text needs
    is supported: `{}` placeholders in order (no indexing, no format specs),
    string-ish and integral arguments. Surplus placeholders (more `{}` than
    arguments) are emitted verbatim; surplus ARGUMENTS append at the end —
    both are visible in the golden rather than silently dropped.
    @tparam Args the argument types (each with a @ref cat_append overload).
    @param f    the format string.
    @param args the values substituted for the `{}` placeholders, in order.
    @return the formatted string. */
template <class... Args>
constexpr std::string cat(std::string_view f, const Args&... args) {
    std::string out{};
    out.reserve(f.size() + sizeof...(Args) * 8);
    std::size_t pos{0};
    [[maybe_unused]] auto emit_next = [&](const auto& a) {
        const std::size_t brace{f.find("{}", pos)};
        out += f.substr(pos, brace - pos); // npos → the whole tail
        cat_append(out, a);
        pos = brace == std::string_view::npos ? f.size() : brace + 2;
    };
    (emit_next(args), ...);
    out += f.substr(pos);
    return out;
}

/** @} */

/** Append @a text as C# `/// ` XML-doc `<summary>` lines, or nothing when empty.
    @param out    the buffer to append to.
    @param indent the leading indentation for each `///` line.
    @param text   the summary text (may be null/empty). */
inline void emit_doc_comment(std::string& out, std::string_view indent,
                             const char* text) {
    if (!text || !*text)
        return;
    out += indent;
    out += "/// <summary>";
    for (const char* c{text}; *c; ++c) {
        if (*c == '\n') {
            out += '\n';
            out += indent;
            out += "/// ";
        } else {
            out += *c;
        }
    }
    out += "</summary>\n";
}

/** Collapse newlines in @a text to spaces (for a single-line `<param>` tail).
    @param text the source text (may be null).
    @return the flattened text, empty if @a text is null. */
inline std::string one_line(const char* text) {
    std::string out{};
    if (!text)
        return out;
    for (const char* c{text}; *c; ++c)
        out += (*c == '\n') ? ' ' : *c;
    return out;
}

/** Escape a C# keyword with the verbatim-identifier prefix (`@int`), so a C++
    parameter named `string` or `base` still yields legal C#.
    @param id the candidate identifier.
    @return @a id, `@`-prefixed when it collides with a C# keyword. */
inline std::string cs_escape(std::string id) {
    static constexpr const char* keywords[]{
        "abstract", "as",       "base",     "bool",      "break",     "byte",
        "case",     "catch",    "char",     "checked",   "class",     "const",
        "continue", "decimal",  "default",  "delegate",  "do",        "double",
        "else",     "enum",     "event",    "explicit",  "extern",    "false",
        "finally",  "fixed",    "float",    "for",       "foreach",   "goto",
        "if",       "implicit", "in",       "int",       "interface", "internal",
        "is",       "lock",     "long",     "namespace", "new",       "null",
        "object",   "operator", "out",      "override",  "params",    "private",
        "protected","public",   "readonly", "ref",       "return",    "sbyte",
        "sealed",   "short",    "sizeof",   "stackalloc","static",    "string",
        "struct",   "switch",   "this",     "throw",     "true",      "try",
        "typeof",   "uint",     "ulong",    "unchecked", "unsafe",    "ushort",
        "using",    "virtual",  "void",     "volatile",  "while",     "value"};
    for (const char* k : keywords)
        if (id == k)
            return "@" + id;
    return id;
}

/** The `[LibraryImport(Lib…)]` attribute, adding UTF-8 string marshalling when
    @a has_string.
    @param has_string whether any parameter or the return is a UTF-8 string.
    @return the attribute text. */
inline std::string import_attr(bool has_string) {
    return has_string
               ? "[LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]"
               : "[LibraryImport(Lib)]";
}

/** Split a `\x1f`-joined parameter-name list back into its pieces (the
    @ref call_pieces::param_names encoding — one flat string travels through the
    emission layers instead of a vector).
    @param joined the `\x1f`-terminated concatenation.
    @return the individual names, in order. */
inline std::vector<std::string> split_param_names(const std::string& joined) {
    std::vector<std::string> out{};
    std::string cur{};
    for (char c : joined) {
        if (c == '\x1f') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

/** Uppercase @a s's first character in place-ish (returning a copy) — the
    name-token convention for a generated wrapper's leaf (`byte` → `Byte`).
    @param s the token (assumed non-empty and ASCII).
    @return the capitalized copy. */
inline std::string capitalized(std::string s) {
    if (!s.empty() && s.front() >= 'a' && s.front() <= 'z')
        s.front() = static_cast<char>(s.front() - ('a' - 'A'));
    return s;
}

} // namespace welder::inline v0::rods::csharp
