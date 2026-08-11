#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <welder/rods/csharp/document/artifacts.hpp>
#include <welder/rods/csharp/text.hpp> // emit_doc_comment

/** @file
    The **class handle** the driver's per-class hooks write into, and the two
    C#-specific rules it settles at flush time.

    The driver has no explicit "finish class" hook, so
    @ref welder::rods::csharp::class_writer is an RAII accumulator: members are
    appended as they are emitted and the assembled `SafeHandle` subclass +
    `public class … : IDisposable` block is written to its sink when the handle
    dies. (The paired native thunks and P/Invoke declarations were already
    appended, flatly, to the document as each member was emitted.)

    Two things can only be decided once the whole class surface is known, and
    both live here:

    - **Comparison pairing.** C# requires `==`/`!=`, `<`/`>` and `<=`/`>=` in
      pairs. Emissions are recorded in a ledger; at flush a partner C++ never
      declared is synthesized (negation for equality, operand swap for a
      homogeneous relational), and one that cannot be synthesized demotes to a
      named method.
    - **Name collisions.** C# forbids a member and a nested type sharing a name
      (CS0102), and welder reserves the leading-underscore namespace for its own
      scaffolding. Both are diagnosed INTO the artifact as a `#error` carrying
      welder's own message and the `weld_as` escape, rather than left to the
      consumer's compiler.
*/

namespace welder::inline v0::rods::csharp {

/** A class handle. Accumulates the wrapper class body (properties, methods,
    constructors) and flushes the assembled `SafeHandle` subclass + `public
    class … : IDisposable` block to its sink on destruction — the driver has no
    explicit "finish class" hook, so RAII is the finalizer. Move-only so a
    moved-from temporary does not double-flush. */
struct class_writer {
    document* doc{nullptr};
    std::string cs_name{};        /**< The C# class name (the leaf). */
    std::string cs_ns{};          /**< The enclosing C# namespace's dotted path
                                       below the root (`""` = root). */
    std::string cs_path{};        /**< The dotted C# path from the root
                                       namespace (`Util.Outer.Inner`). */
    std::string cpp_anchor{};     /**< The `^^…` / lookup expression the shim
                                       anchors this type's thunks on. */
    std::string* sink{nullptr};   /**< Flush target: an outer class's members
                                       buffer for a NESTED type, else null =
                                       the document's types section. */
    std::string doc_text{};       /**< The class summary doc. */
    std::string cpp_qualified{};  /**< The `::`-qualified C++ type (anchor spelling). */
    std::string sym_prefix{};     /**< The `welder_<path>` C-symbol prefix. */
    std::string destroy_symbol{}; /**< The `welder_…_destroy` symbol. */
    std::string handle_field{};   /**< This level's handle field (`_h_<Class>`). */
    std::string handle_cs{};      /**< The handle class's C# TYPE spelling
                                       (`Outer.InnerHandle` for nested). */
    std::string base_ref{};       /**< First welded base's placeholder ref, or empty. */
    std::string base_upcast_sym{};/**< The `welder_<D>_as_<B>` symbol for it. */
    bool is_director{false};      /**< Emit the director machinery for this class. */
    std::string director_ident{}; /**< The C++ director struct's identifier. */
    /** One overridable slot, as make_class recorded it: identifier + the full
        function-type display (unique per signature) + the slot index. The
        method sweep matches its callables against this at emission time —
        add_method has no compile-time handle on the welded type. */
    struct vslot {
        const char* name; /**< The virtual's identifier. */
        const char* sig;  /**< Its function type's display string. */
        std::size_t k;    /**< Its slot index. */
    };
    std::vector<vslot> vslots{};
    /** Emitted member (property/method) names and nested TYPE names: C#
        forbids a member and a nested type sharing a name (CS0102), so the
        flush diagnoses the collision INTO the artifact (a `#error` with a
        designed message — the record_symbol precedent) instead of leaving the
        consumer a bare compiler error. */
    std::vector<std::string> surface_names{};
    std::vector<std::string> nested_names{};
    std::string members{};        /**< Accumulated property/method/ctor text. */

    /** One recorded comparison-operator emission, held back until flush: C#
        requires `==`/`!=`, `<`/`>` and `<=`/`>=` in PAIRS, so pairing is
        decided over the whole class surface — a partner C++ never declared is
        synthesized (negation / operand swap), and a heterogeneous relational
        whose partner cannot be synthesized demotes to a named method. */
    struct cs_comparison {
        std::string op;   /**< The C# token (`"=="`, `"<"`, …). */
        std::string lhs;  /**< The left operand's public C# spelling. */
        std::string rhs;  /**< The right operand's public C# spelling. */
        std::string ret;  /**< The C# return type (usually `bool`). */
        std::string body; /**< The operator body (params are `l` and `r`). */
    };
    std::vector<cs_comparison> comparisons{};
    std::vector<std::string> indexer_sigs{}; /**< Emitted indexer param lists
        (dedup: a const/non-const C++ pair is one C# indexer). */

    class_writer() = default;
    class_writer(const class_writer&) = delete;
    class_writer& operator=(const class_writer&) = delete;
    class_writer(class_writer&& o) noexcept { *this = std::move(o); }
    class_writer& operator=(class_writer&& o) noexcept {
        doc = o.doc;
        cs_name = std::move(o.cs_name);
        cs_ns = std::move(o.cs_ns);
        cs_path = std::move(o.cs_path);
        sink = o.sink;
        doc_text = std::move(o.doc_text);
        cpp_qualified = std::move(o.cpp_qualified);
        cpp_anchor = std::move(o.cpp_anchor);
        sym_prefix = std::move(o.sym_prefix);
        destroy_symbol = std::move(o.destroy_symbol);
        handle_field = std::move(o.handle_field);
        handle_cs = std::move(o.handle_cs);
        base_ref = std::move(o.base_ref);
        base_upcast_sym = std::move(o.base_upcast_sym);
        is_director = o.is_director;
        director_ident = std::move(o.director_ident);
        vslots = std::move(o.vslots);
        surface_names = std::move(o.surface_names);
        nested_names = std::move(o.nested_names);
        members = std::move(o.members);
        comparisons = std::move(o.comparisons);
        indexer_sigs = std::move(o.indexer_sigs);
        o.doc = nullptr;
        return *this;
    }

    /** Whether a comparison with this exact shape was recorded.
        @param op  the C# operator token.
        @param lhs the left operand's spelling.
        @param rhs the right operand's spelling.
        @return true when the ledger already holds it. */
    bool have_comparison(std::string_view op, std::string_view lhs,
                         std::string_view rhs) const {
        for (const auto& c : comparisons)
            if (c.op == op && c.lhs == lhs && c.rhs == rhs)
                return true;
        return false;
    }

    /** Render the recorded comparisons with C#'s pairing rules applied.
        @return the operator (and demoted-method) text for the class body. */
    std::string flush_comparisons() const {
        auto partner = [](std::string_view op) -> const char* {
            if (op == "==") return "!=";
            if (op == "!=") return "==";
            if (op == "<") return ">";
            if (op == ">") return "<";
            if (op == "<=") return ">=";
            return "<=";
        };
        auto demoted = [](std::string_view op) -> const char* {
            if (op == "<") return "LessThan";
            if (op == ">") return "GreaterThan";
            if (op == "<=") return "LessThanOrEqual";
            if (op == ">=") return "GreaterThanOrEqual";
            return op == "==" ? "EqualsValue" : "NotEqualsValue";
        };
        std::string out{};
        for (const auto& c : comparisons) {
            const bool equality{c.op == "==" || c.op == "!="};
            const bool can_pair{have_comparison(partner(c.op), c.lhs, c.rhs) ||
                                (c.ret == "bool" &&
                                 (equality || c.lhs == c.rhs))};
            // Homogeneous wrapper equality gets the C# null protocol (a
            // wrapper is a reference type; `p == null` must not NRE).
            std::string guard{};
            if (equality && c.lhs == c.rhs && c.lhs.front() == '\x01')
                guard = c.op == "=="
                            ? "            if (ReferenceEquals(l, r)) return "
                              "true;\n            if (l is null || r is null) "
                              "return false;\n"
                            : "            if (ReferenceEquals(l, r)) return "
                              "false;\n            if (l is null || r is null) "
                              "return true;\n";
            const std::string q{guard.empty() ? "" : "?"};
            if (can_pair) {
                out += "        public static " + c.ret + " operator " + c.op +
                       "(" + c.lhs + q + " l, " + c.rhs + q +
                       " r)\n        {\n" + guard + c.body + "        }\n\n";
            } else {
                // Unpairable (a lone heterogeneous relational, or a non-bool
                // comparison): a named method instead of an operator.
                out += "        public static " + c.ret + " " + demoted(c.op) +
                       "(" + c.lhs + " l, " + c.rhs + " r)\n        {\n" +
                       c.body + "        }\n\n";
            }
        }
        // Synthesize the missing partners of pairable emissions.
        for (const auto& c : comparisons) {
            const bool equality{c.op == "==" || c.op == "!="};
            if (have_comparison(partner(c.op), c.lhs, c.rhs))
                continue;
            if (c.ret != "bool" || !(equality || c.lhs == c.rhs))
                continue; // was demoted above
            const std::string p{partner(c.op)};
            const std::string pq{(equality && c.lhs == c.rhs &&
                                  c.lhs.front() == '\x01')
                                     ? "?"
                                     : ""};
            out += "        public static bool operator " + p + "(" + c.lhs +
                   pq + " l, " + c.rhs + pq + " r) => ";
            if (equality)
                out += "!(l " + c.op + " r);\n";
            else // homogeneous relational: swap the operands
                out += "r " + c.op + " l;\n";
            out += "\n";
        }
        // == over the class itself: give Equals/GetHashCode their overrides
        // (silencing CS0660/CS0661). The hash is reference identity — C++ has
        // no hash slot to mirror; equal VALUES may hash differently.
        const std::string self_ph{std::string{"\x01"} + cpp_qualified + "\x02"};
        if (have_comparison("==", self_ph, self_ph)) {
            out += "        public override bool Equals(object? obj) => obj is " +
                   cs_name + " _o && this == _o;\n";
            out += "        /// <summary>Reference-identity hash (the C++ type "
                   "has no hash to mirror).</summary>\n";
            out += "        public override int GetHashCode() => "
                   "base.GetHashCode();\n\n";
        }
        return out;
    }

    ~class_writer() {
        if (!doc)
            return;
        std::string& out{sink ? *sink : doc->section(cs_ns).types};
        // The per-class SafeHandle: ReleaseHandle calls the destroy thunk, so
        // finalization and Dispose share one release path.
        out += "    internal sealed class " + cs_name +
               "Handle : SafeHandle\n    {\n"
               "        internal " + cs_name +
               "Handle(IntPtr handle, bool owns) : base(IntPtr.Zero, owns)\n"
               "        {\n            SetHandle(handle);\n        }\n"
               "        public override bool IsInvalid => handle == "
               "IntPtr.Zero;\n"
               "        protected override bool ReleaseHandle()\n"
               "        {\n            NativeMethods." + destroy_symbol +
               "(handle);\n            return true;\n        }\n"
               "    }\n\n";
        emit_doc_comment(out, "    ", doc_text.empty() ? nullptr : doc_text.c_str());
        // Unsealed: another welded type may derive (and the directors phase
        // needs subclassable wrappers anyway).
        out += "    public class " + cs_name + " : " +
               (base_ref.empty() ? std::string{"IDisposable"} : base_ref) +
               "\n    {\n";
        // Per-LEVEL handle: this level's field holds the address of ITS base
        // subobject (the derived constructor chains an upcast down), so a
        // base-typed parameter always passes the correctly-adjusted pointer —
        // multiple inheritance included.
        out += "        internal " + cs_name + "Handle " + handle_field + ";\n";
        if (base_ref.empty()) {
            // The reference_internal anchor: a view stores its parent here so
            // the parent cannot be collected while the view lives. Declared on
            // the hierarchy root only (derived levels inherit it).
            out += "        internal object? _owner;\n";
            // Whether this instance was constructed from C# (a director): its
            // virtual-slot methods then take the qualified base-call path.
            out += "        internal bool _isDirector;\n";
            out += "        internal " + cs_name + "(IntPtr handle, bool owns) { " +
                   handle_field + " = new " + cs_name +
                   "Handle(handle, owns); }\n\n";
        } else {
            // Chain the UPCAST pointer to the base level (non-owning there —
            // the most-derived level owns and destroys via ITS destructor).
            out += "        internal " + cs_name +
                   "(IntPtr handle, bool owns) : base(NativeMethods." +
                   base_upcast_sym + "(handle), false) { " + handle_field +
                   " = new " + cs_name + "Handle(handle, owns); }\n\n";
        }
        // A member or nested-type name beginning with '_' would land in the
        // namespace of the generated scaffolding (_h_*, _owner, _isDirector,
        // _New*, _Slot*, ...) — an underscore-led C++ identifier restyles to
        // one (`_leading` -> `_Leading`), and a weld_as can spell one
        // verbatim. Reserved, diagnosed with the escape named.
        for (const auto* names : {&surface_names, &nested_names})
            for (const auto& n : *names)
                if (!n.empty() && n.front() == '_')
                    out += "#error welder: the C# name '" + n + "' bound on '" +
                           cs_path +
                           "' begins with an underscore, which is reserved for "
                           "welder's generated scaffolding; rename the member, "
                           "or give it a [[=welder::weld_as]] that does not "
                           "start with '_'\n";
        // The nested-type/member name collision (C# CS0102), diagnosed here
        // with welder's message rather than left to the consumer's compiler.
        for (const auto& n : nested_names)
            for (const auto& m : surface_names)
                if (n == m)
                    out += "#error welder: the nested type '" + cs_path + "." +
                           n + "' and a bound member of '" + cs_path +
                           "' share the C# name '" + n +
                           "' (C# forbids this, CS0102); rename one side with "
                           "[[=welder::weld_as]]\n";
        out += members;
        out += flush_comparisons();
        if (base_ref.empty())
            out += "        public virtual void Dispose() => " + handle_field +
                   ".Dispose();\n";
        else
            out += "        public override void Dispose() { " + handle_field +
                   ".Dispose(); base.Dispose(); }\n";
        out += "    }\n\n";
    }
};

} // namespace welder::inline v0::rods::csharp
