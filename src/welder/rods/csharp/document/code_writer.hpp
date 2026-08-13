#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include <welder/rods/csharp/document/artifacts.hpp>
#include <welder/rods/csharp/text.hpp> // cat

/** @file
    **How emitted text is written**: the indentation-aware
    @ref welder::rods::csharp::code_writer every emitter assembles source text
    through, and the @ref welder::rods::csharp::bound_symbol that makes the
    backend's central invariant — one C symbol produces one *coordinated triple*
    (native thunk, P/Invoke declaration, managed wrapper) — a structural fact
    rather than a convention.

    Before these two types, every emitter appended to the document's raw string
    buffers directly (`w.doc->shim += …`), respelling the indentation by hand
    and remembering to call `record_symbol` itself; the triple existed only as a
    discipline. Now an emitter *asks the symbol for its sinks*: a
    @ref welder::rods::csharp::bound_symbol registers the symbol on
    construction (the collision check cannot be forgotten) and hands out one
    positioned @ref welder::rods::csharp::code_writer per artifact — so text
    for a symbol can only land in the places that symbol coordinates.
*/

namespace welder::inline v0::rods::csharp {

/** An indentation-aware writer over one text buffer.

    Wraps a target buffer with a current depth (in 4-space units) and a `{}`
    mini-format (@ref welder::rods::csharp::cat), so an emitter states *what*
    a line says and *how deep* it sits instead of concatenating indent strings
    by hand. Copyable and cheap — a positioned writer is a value handed out by
    @ref bound_symbol / created ad hoc over any buffer. */
class code_writer {
  public:
    /** Position a writer over @a out at @a depth.
        @param out   the buffer written to (must outlive the writer).
        @param depth the current indentation, in 4-space units. */
    explicit code_writer(std::string& out, std::size_t depth = 0)
        : out_{&out}, depth_{depth} {}

    /** Write one indented line: the current indent, then @a f formatted with
        @a args (see @ref cat), then a newline.
        @tparam Args the format-argument types.
        @param f    the `{}` format string.
        @param args the values substituted, in order. */
    template <class... Args>
    void line(std::string_view f, const Args&... args) {
        indent();
        *out_ += cat(f, args...);
        *out_ += '\n';
    }

    /** Append formatted text verbatim — no indent, no newline (for building a
        line in pieces).
        @tparam Args the format-argument types.
        @param f    the `{}` format string.
        @param args the values substituted, in order. */
    template <class... Args>
    void text(std::string_view f, const Args&... args) {
        *out_ += cat(f, args...);
    }

    /** Append @a s completely verbatim — pre-indented multi-line text (e.g. a
        @ref call_pieces::wrap staging block) that manages its own layout.
        @param s the text. */
    void raw(std::string_view s) { *out_ += s; }

    /** Write one empty line (the blank separating members / thunks). */
    void blank() { *out_ += '\n'; }

    /** The RAII brace scope @ref braces returns: `{` on construction, `}` on
        destruction, one level deeper in between. Immovable — it pins the
        writer's depth for exactly one lexical scope. */
    class brace_scope {
      public:
        /** Open the scope: write the `{` line and deepen @a w by one.
            @param w the writer the scope indents. */
        explicit brace_scope(code_writer& w) : w_{w} {
            w_.line("{");
            ++w_.depth_;
        }
        brace_scope(const brace_scope&) = delete;
        brace_scope& operator=(const brace_scope&) = delete;
        /** Close the scope: shallow @ref w_ back out and write the `}` line. */
        ~brace_scope() {
            --w_.depth_;
            w_.line("}");
        }

      private:
        code_writer& w_; /**< The writer whose depth this scope owns. */
    };

    /** Open a brace scope at the current depth (C# style — the `{` on its own
        line): everything written while the returned guard lives is one level
        deeper, and the matching `}` lands when it dies.
        @return the RAII scope. */
    [[nodiscard]] brace_scope braces() { return brace_scope{*this}; }

    /** A writer over the same buffer, @a delta levels deeper — for a nested
        region whose depth outlives no scope (director bodies, `finally` arms).
        @param delta the additional depth.
        @return the deeper writer. */
    [[nodiscard]] code_writer deeper(std::size_t delta = 1) const {
        return code_writer{*out_, depth_ + delta};
    }

    /** The current indentation as text (for handing to legacy helpers that
        still take an indent string, e.g. @ref emit_doc_comment).
        @return `4 * depth` spaces. */
    [[nodiscard]] std::string indentation() const {
        return std::string(4 * depth_, ' ');
    }

  private:
    /** Write the current indent. */
    void indent() { out_->append(4 * depth_, ' '); }

    std::string* out_;  /**< The target buffer. */
    std::size_t depth_; /**< The current depth, in 4-space units. */
};

/** One emitted C symbol and its three coordinated sinks.

    Constructing a `bound_symbol` *is* the symbol's registration
    (@ref document::record_symbol — the collision diagnostic cannot be skipped);
    the three artifact writers are then obtained from it, positioned where that
    artifact's text belongs. An emitter therefore cannot add a thunk and forget
    its P/Invoke declaration to a *different* symbol — all three sinks share
    this one's name.

    The wrapper sink varies by call site (a class's members buffer, a
    namespace's `Global` statics) and so is supplied by the emitter; the thunk
    and P/Invoke sinks are fixed by the document. */
class bound_symbol {
  public:
    /** Register @a sym and bind its sinks.
        @param doc           the two-artifact document.
        @param sym           the C symbol (recorded; a duplicate diagnoses).
        @param wrapper_sink  the buffer the managed wrapper lands in.
        @param wrapper_depth the wrapper text's indentation, in 4-space units. */
    bound_symbol(document& doc, std::string sym, std::string& wrapper_sink,
                 std::size_t wrapper_depth)
        : doc_{&doc}, sym_{std::move(sym)}, wrapper_sink_{&wrapper_sink},
          wrapper_depth_{wrapper_depth} {
        doc_->record_symbol(sym_);
    }

    /** The C symbol all three sinks coordinate on.
        @return the symbol text. */
    [[nodiscard]] const std::string& name() const { return sym_; }

    /** The native-thunk sink (the shim buffer, top level).
        @return a positioned writer. */
    [[nodiscard]] code_writer thunk() const { return code_writer{doc_->shim, 0}; }

    /** The `[LibraryImport]` declaration sink (inside `NativeMethods`).
        @return a positioned writer. */
    [[nodiscard]] code_writer pinvoke() const {
        return code_writer{doc_->pinvoke, 2};
    }

    /** The managed-wrapper sink this symbol was bound with.
        @return a positioned writer. */
    [[nodiscard]] code_writer wrapper() const {
        return code_writer{*wrapper_sink_, wrapper_depth_};
    }

  private:
    document* doc_;             /**< The shared document. */
    std::string sym_;           /**< The registered C symbol. */
    std::string* wrapper_sink_; /**< Where this symbol's wrapper text lands. */
    std::size_t wrapper_depth_; /**< The wrapper text's depth. */
};

} // namespace welder::inline v0::rods::csharp
