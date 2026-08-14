#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <welder/rods/csharp/document/artifacts.hpp>
#include <welder/rods/csharp/document/code_writer.hpp>
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
    document* doc{nullptr};       /**< The shared document (null = moved-from). */
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
    std::string type_token{};     /**< The bound type's @ref welder::rods::csharp::symbol_token
                                       — what tells a member DECLARED here from
                                       one flattened in from a base, whose
                                       declaring scope may have no name at all. */
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
    std::vector<vslot> vslots{};  /**< The type's overridable slots (director). */
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
        type_token = std::move(o.type_token);
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
        code_writer w{out, 2};
        for (const auto& c : comparisons) {
            const bool equality{c.op == "==" || c.op == "!="};
            const bool can_pair{have_comparison(partner(c.op), c.lhs, c.rhs) ||
                                (c.ret == "bool" &&
                                 (equality || c.lhs == c.rhs))};
            // Homogeneous wrapper equality gets the C# null protocol (a
            // wrapper is a reference type; `p == null` must not NRE).
            std::string guard{};
            if (equality && c.lhs == c.rhs && c.lhs.front() == '\x01') {
                code_writer g{guard, 3};
                g.line("if (ReferenceEquals(l, r)) return {};",
                       c.op == "==" ? "true" : "false");
                g.line("if (l is null || r is null) return {};",
                       c.op == "==" ? "false" : "true");
            }
            const std::string q{guard.empty() ? "" : "?"};
            if (can_pair) {
                w.line("public static {} operator {}({}{} l, {}{} r)", c.ret,
                       c.op, c.lhs, q, c.rhs, q);
                {
                    const auto body{w.braces()};
                    w.raw(guard);
                    w.raw(c.body);
                }
                w.blank();
            } else {
                // Unpairable (a lone heterogeneous relational, or a non-bool
                // comparison): a named method instead of an operator.
                w.line("public static {} {}({} l, {} r)", c.ret,
                       demoted(c.op), c.lhs, c.rhs);
                {
                    const auto body{w.braces()};
                    w.raw(c.body);
                }
                w.blank();
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
            if (equality)
                w.line("public static bool operator {}({}{} l, {}{} r) => "
                       "!(l {} r);",
                       p, c.lhs, pq, c.rhs, pq, c.op);
            else // homogeneous relational: swap the operands
                w.line("public static bool operator {}({}{} l, {}{} r) => "
                       "r {} l;",
                       p, c.lhs, pq, c.rhs, pq, c.op);
            w.blank();
        }
        // == over the class itself: give Equals/GetHashCode their overrides
        // (silencing CS0660/CS0661). The hash is reference identity — C++ has
        // no hash slot to mirror; equal VALUES may hash differently.
        const std::string self_ph{std::string{"\x01"} + cpp_qualified + "\x02"};
        if (have_comparison("==", self_ph, self_ph)) {
            w.line("public override bool Equals(object? obj) => obj is {} _o "
                   "&& this == _o;",
                   cs_name);
            w.line("/// <summary>Reference-identity hash (the C++ type has no "
                   "hash to mirror).</summary>");
            w.line("public override int GetHashCode() => base.GetHashCode();");
            w.blank();
        }
        return out;
    }

    ~class_writer() {
        if (!doc)
            return;
        std::string& out{sink ? *sink : doc->section(cs_ns).types};
        code_writer w{out, 1};
        // The per-class SafeHandle: ReleaseHandle calls the destroy thunk, so
        // finalization and Dispose share one release path.
        w.line("internal sealed class {}Handle : SafeHandle", cs_name);
        {
            const auto handle_cls{w.braces()};
            w.line("internal {}Handle(IntPtr handle, bool owns) : "
                   "base(IntPtr.Zero, owns)",
                   cs_name);
            {
                const auto body{w.braces()};
                w.line("SetHandle(handle);");
            }
            w.line("public override bool IsInvalid => handle == IntPtr.Zero;");
            w.line("protected override bool ReleaseHandle()");
            {
                const auto body{w.braces()};
                w.line("NativeMethods.{}(handle);", destroy_symbol);
                w.line("return true;");
            }
        }
        w.blank();
        emit_doc_comment(out, "    ",
                         doc_text.empty() ? nullptr : doc_text.c_str());
        // Unsealed: another welded type may derive (and the directors phase
        // needs subclassable wrappers anyway).
        w.line("public class {} : {}", cs_name,
               base_ref.empty() ? std::string{"IDisposable"} : base_ref);
        {
            const auto cls{w.braces()};
            // Per-LEVEL handle: this level's field holds the address of ITS
            // base subobject (the derived constructor chains an upcast down),
            // so a base-typed parameter always passes the correctly-adjusted
            // pointer — multiple inheritance included.
            w.line("internal {}Handle {};", cs_name, handle_field);
            if (base_ref.empty()) {
                // The reference_internal anchor: a view stores its parent here
                // so the parent cannot be collected while the view lives.
                // Declared on the hierarchy root only (derived levels inherit
                // it).
                w.line("internal object? _owner;");
                // Whether this instance was constructed from C# (a director):
                // its virtual-slot methods then take the qualified base-call
                // path.
                w.line("internal bool _isDirector;");
                w.line("internal {}(IntPtr handle, bool owns) { {} = new "
                       "{}Handle(handle, owns); }",
                       cs_name, handle_field, cs_name);
                w.blank();
            } else {
                // Chain the UPCAST pointer to the base level (non-owning
                // there — the most-derived level owns and destroys via ITS
                // destructor).
                w.line("internal {}(IntPtr handle, bool owns) : "
                       "base(NativeMethods.{}(handle), false) { {} = new "
                       "{}Handle(handle, owns); }",
                       cs_name, base_upcast_sym, handle_field, cs_name);
                w.blank();
            }
            // A member or nested-type name beginning with '_' would land in
            // the namespace of the generated scaffolding (_h_*, _owner,
            // _isDirector, _New*, _Slot*, ...) — an underscore-led C++
            // identifier restyles to one (`_leading` -> `_Leading`), and a
            // weld_as can spell one verbatim. Reserved, diagnosed with the
            // escape named. (#error lines sit at column 0, outside the
            // writer's depth.)
            code_writer diag{out, 0};
            for (const auto* names : {&surface_names, &nested_names})
                for (const auto& n : *names)
                    if (!n.empty() && n.front() == '_')
                        diag.line(
                            "#error welder: the C# name '{}' bound on '{}' "
                            "begins with an underscore, which is reserved for "
                            "welder's generated scaffolding; rename the "
                            "member, or give it a [[=welder::weld_as]] that "
                            "does not start with '_'",
                            n, cs_path);
            // The nested-type/member name collision (C# CS0102), diagnosed
            // here with welder's message rather than left to the consumer's
            // compiler.
            for (const auto& n : nested_names)
                for (const auto& m : surface_names)
                    if (n == m)
                        diag.line(
                            "#error welder: the nested type '{}.{}' and a "
                            "bound member of '{}' share the C# name '{}' (C# "
                            "forbids this, CS0102); rename one side with "
                            "[[=welder::weld_as]]",
                            cs_path, n, cs_path, n);
            w.raw(members);
            w.raw(flush_comparisons());
            if (base_ref.empty())
                w.line("public virtual void Dispose() => {}.Dispose();",
                       handle_field);
            else
                w.line("public override void Dispose() { {}.Dispose(); "
                       "base.Dispose(); }",
                       handle_field);
        }
        w.blank();
    }
};

} // namespace welder::inline v0::rods::csharp
