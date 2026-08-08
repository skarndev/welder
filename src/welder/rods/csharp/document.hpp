#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <welder/rods/csharp/type_map.hpp> // emit_doc_comment / one_line

/** @file
    The C#/.NET backend's document assembler: the two-stream document and the RAII
    writer handles the driver's module/class/enum handles deduce to.

    The C# rod is the one backend that emits **two coordinated artifacts** from a
    single driver pass: a native `extern "C"` **shim** (`shim.cpp`, compiled into a
    shared library with reflection enabled, against the same welded header) and a
    managed **C# wrapper** (`Bindings.cs`, `[LibraryImport]` P/Invoke declarations
    + idiomatic classes). So @ref document holds several buffers rather than
    luacats' one, and @ref document::render_shim / @ref document::render_cs
    assemble the two files. Every emission primitive appends the paired native
    thunk and managed declaration together, keyed by the same symbol, so the two
    sides cannot desync; a duplicate symbol is caught at render time.

    The managed scaffolding rendered once per `Bindings.cs`:
    - `WelderError` — the blittable mirror of the shim's trailing error out-param;
    - `WelderInterop.ThrowIfError` — reads the slot, frees the message, and
      throws: codes 2–5 map to the matching BCL exception types
      (`OutOfMemoryException` / `ArgumentException` /
      `ArgumentOutOfRangeException` / `ArithmeticException`), everything else
      to @c WelderNativeException;
    - one `SafeHandle` subclass per welded class (its `ReleaseHandle` calls the
      class's destroy thunk), giving every wrapper finalizer-safe ownership and
      premature-collection safety on every P/Invoke that passes it.
*/

namespace welder::inline v0::rods::csharp {

/** Knobs the generator supplies once for the whole module. */
struct options {
    std::string cs_namespace{}; /**< The root C# namespace (the module name). */
    std::string library{};      /**< The P/Invoke library (the shared-lib base name). */
    std::string shim_include{}; /**< The header the shim `#include`s to see the types. */
};

/** A C# `static class` collecting one C++ namespace's free functions and variables
    (welded *types* are emitted flat in the module namespace in v1). */
struct static_class {
    std::string name{}; /**< The C# static-class name (the namespace identifier). */
    std::string body{}; /**< Accumulated static members. */
};

/** The growing pair of documents shared by every writer handle: the native shim and
    the managed wrapper. Class/enum text lands in @ref types; free functions and
    namespace variables land in a per-namespace @ref static_class; every P/Invoke
    declaration lands in @ref pinvoke (one `NativeMethods` class); every native thunk
    lands in @ref shim. */
struct document {
    options opts{};
    std::string module_doc{}; /**< The root namespace's doc (a file-header comment). */
    std::string shim{};    /**< `extern "C"` thunk bodies. */
    std::string directors{}; /**< Director subclass definitions (before extern "C"). */
    std::string pinvoke{}; /**< `[LibraryImport]` declarations (inside NativeMethods). */
    std::string types{};   /**< Wrapper class + enum declarations (flat in the ns). */
    std::vector<static_class> statics{}; /**< Per-namespace function/variable holders. */
    std::string containers{};   /**< Generated container-wrapper classes. */
    std::vector<std::string> container_keys{}; /**< Dedup (one wrapper per type). */

    /** First-emission check for a generated container wrapper. */
    bool claim_container(std::string key) {
        for (const auto& k : container_keys)
            if (k == key)
                return false;
        container_keys.push_back(std::move(key));
        return true;
    }
    std::vector<std::string> symbols{};  /**< Every emitted C symbol (collision check). */
    /** raw `::qualified` C++ name → final C# name, filled by make_class/make_enum.
        Type REFERENCES are emitted as `\x01raw\x02` placeholders and reconciled
        at render (the luacats record_type_name idiom) — declaration order never
        matters, and hooks without a Style (add_operator) still spell styled
        names correctly. */
    std::vector<std::pair<std::string, std::string>> type_names{};

    /** Register (or update — nested factories refine the flat name to the
        dotted path) the final C# name for raw C++ type spelling @a raw. */
    void record_type_name(std::string raw, std::string styled) {
        for (auto& [r, cs] : type_names)
            if (r == raw) {
                cs = std::move(styled);
                return;
            }
        type_names.emplace_back(std::move(raw), std::move(styled));
    }

    /** Resolve every placeholder in @a text against @ref type_names: the
        `\x01raw\x02` flavor substitutes the registered C# name verbatim
        (`Outer.Inner` for a nested type), the `\x03raw\x04` flavor its
        IDENTIFIER-SAFE form (dots → underscores — handle-field names). An
        unmatched one keeps the raw name — visibly wrong C#, which cannot
        happen for gate-admitted references. */
    std::string apply_type_renames(std::string text) const {
        for (const char open : {'\x01', '\x03'}) {
            const char close{open == '\x01' ? '\x02' : '\x04'};
            std::size_t b1{0};
            while ((b1 = text.find(open, b1)) != std::string::npos) {
                const std::size_t b2{text.find(close, b1 + 1)};
                if (b2 == std::string::npos)
                    break;
                const std::string raw{text.substr(b1 + 1, b2 - b1 - 1)};
                std::string sub{raw};
                for (const auto& [r, cs] : type_names)
                    if (r == raw) {
                        sub = cs;
                        break;
                    }
                if (open == '\x03')
                    for (char& c : sub)
                        if (c == '.')
                            c = '_';
                text.replace(b1, b2 - b1 + 1, sub);
                // Do NOT skip past the substitution: a container's final name
                // itself contains its element's placeholder (the rescan
                // resolves it; keys never contain themselves, so this
                // terminates).
            }
        }
        return text;
    }

    /** Record an emitted C symbol; a duplicate aborts the generator with a
        designed message (two members whose underscore paths collide — e.g.
        `a_b::c` vs `a::b_c` — would silently link to one thunk otherwise). */
    void record_symbol(std::string sym) {
        for (const auto& s : symbols)
            if (s == sym) {
                shim += "#error welder: duplicate C symbol '" + sym +
                        "' (colliding underscore paths); rename one entity with "
                        "weld_as\n";
                return;
            }
        symbols.push_back(std::move(sym));
    }

    /** The accumulation buffer for the free functions/variables of the C# static
        class named @a name (created on first use).
        @param name the static-class name.
        @return a reference to its members buffer. */
    std::string& static_body(std::string_view name) {
        for (auto& s : statics)
            if (s.name == name)
                return s.body;
        statics.push_back({std::string{name}, {}});
        return statics.back().body;
    }

    /** The finished `shim.cpp` text. */
    std::string render_shim() const {
        std::string out{
            "// <auto-generated> welder C#/.NET native shim. Do not edit -\n"
            "// regenerate via the welder_csharp_generate_bindings() target.\n"
            "// Compiled with reflection against the same welded header the\n"
            "// generator saw: thunk bodies SPLICE the exact member reflection\n"
            "// (re-derived by the shared lookup layer), so no C++ type is\n"
            "// respelled here - only the C-ABI wire types are text.\n"
            "#include <cstdint>\n"
            "#include <welder/vocabulary.hpp>\n"};
        if (!opts.shim_include.empty())
            out += "#include \"" + opts.shim_include + "\"\n";
        out += "#include <welder/rods/csharp/shim_support.hpp>\n"
               "\n"
               "namespace wcs = ::welder::rods::csharp;\n"
               "\n";
        out += directors;
        out += "extern \"C\" {\n\n";
        out += shim;
        out += "void welder_free(void* p) { std::free(p); }\n\n"
               "const char* welder_dup_utf8(const char* s) { return "
               "wcs::shim::dup(s ? s : \"\"); }\n\n";
        out += "} // extern \"C\"\n";
        return out;
    }

    /** The finished `Bindings.cs` text. */
    std::string render_cs() const {
        std::string out{
            "// <auto-generated> welder C#/.NET bindings. Do not edit -\n"
            "// regenerate via the welder_csharp_generate_bindings() target.\n"};
        if (!module_doc.empty()) {
            out += "//\n";
            for (std::size_t b{0}; b < module_doc.size();) {
                std::size_t e{module_doc.find('\n', b)};
                if (e == std::string::npos)
                    e = module_doc.size();
                out += "// " + module_doc.substr(b, e - b) + "\n";
                b = e + 1;
            }
        }
        out += "#nullable enable\n"
               "using System;\n"
               "using System.Runtime.CompilerServices;\n"
               "using System.Runtime.InteropServices;\n\n";
        out += "namespace " + opts.cs_namespace + "\n{\n";
        // The error contract: the blittable slot + the check-and-throw helper.
        out +=
            "    /// <summary>The native error slot every welder thunk fills "
            "(code 0 = success).</summary>\n"
            "    [StructLayout(LayoutKind.Sequential)]\n"
            "    internal struct WelderError\n    {\n"
            "        public int Code;\n"
            "        public IntPtr Message;\n"
            "    }\n\n"
            "    /// <summary>A C++ exception that crossed the native "
            "boundary.</summary>\n"
            "    public class WelderNativeException : Exception\n    {\n"
            "        public int NativeCode { get; }\n"
            "        public WelderNativeException(int code, string message) : "
            "base(message)\n"
            "        {\n            NativeCode = code;\n        }\n"
            "    }\n\n"
            "    /// <summary>The by-value wire of an optional with a leaf "
            "payload.</summary>\n"
            "    [StructLayout(LayoutKind.Sequential)]\n"
            "    internal struct WelderOptWire\n    {\n"
            "        public byte Has;\n"
            "        public long I;\n"
            "        public double F;\n"
            "        public IntPtr S;\n"
            "        public IntPtr P;\n"
            "    }\n\n"
            "    /// <summary>The by-value wire of a scalar/enum sequence "
            "(buffer + length).</summary>\n"
            "    [StructLayout(LayoutKind.Sequential)]\n"
            "    internal struct WelderSeqWire\n    {\n"
            "        public IntPtr Data;\n"
            "        public long Len;\n"
            "    }\n\n"
            "    internal static class WelderInterop\n    {\n"
            "        internal static void ThrowIfError(in WelderError err)\n"
            "        {\n"
            "            if (err.Code == 0) return;\n"
            "            string msg = \"\";\n"
            "            if (err.Message != IntPtr.Zero)\n"
            "            {\n"
            "                msg = Marshal.PtrToStringUTF8(err.Message) ?? "
            "\"\";\n"
            "                NativeMethods.welder_free(err.Message);\n"
            "            }\n"
            "            throw err.Code switch\n"
            "            {\n"
            "                2 => new OutOfMemoryException(msg),\n"
            "                3 => new ArgumentException(msg),\n"
            "                4 => new ArgumentOutOfRangeException(null, msg),\n"
            "                5 => new ArithmeticException(msg),\n"
            "                _ => (Exception)new WelderNativeException(err.Code, "
            "msg),\n"
            "            };\n"
            "        }\n"
            "    }\n\n";
        // The P/Invoke surface: one partial static class (LibraryImport needs partial).
        out += "    internal static partial class NativeMethods\n    {\n";
        out += "        internal const string Lib = \"" + opts.library + "\";\n\n";
        out += pinvoke;
        out += "        [LibraryImport(Lib)] internal static partial void "
               "welder_free(IntPtr p);\n";
        out += "        [LibraryImport(Lib, StringMarshalling = "
               "StringMarshalling.Utf8)] internal static partial IntPtr "
               "welder_dup_utf8(string s);\n";
        out += "    }\n\n";
        out += types;
        out += containers;
        for (const auto& s : statics) {
            out += "    public static class " + s.name + "\n    {\n";
            out += s.body;
            out += "    }\n\n";
        }
        out += "}\n";
        return apply_type_renames(std::move(out));
    }
};

/** A module handle: the shared document plus the C# static-class name that this
    namespace's free functions/variables accumulate into. Copyable (the driver's
    `add_submodule` returns one by value). */
struct module_writer {
    document* doc{nullptr};
    std::string cs_class{}; /**< This namespace's C# static-class name. */
};

/** A class handle. Accumulates the wrapper class body (properties, methods,
    constructors) and flushes the assembled `SafeHandle` subclass + `public sealed
    class … : IDisposable` block to @ref document::types on destruction — the
    driver has no explicit "finish class" hook, so RAII is the finalizer.
    Move-only so a moved-from temporary does not double-flush. The paired native
    thunks and P/Invoke declarations were already appended (flatly) to the
    document as each member was emitted. */
struct class_writer {
    document* doc{nullptr};
    std::string cs_name{};        /**< The C# class name (the leaf). */
    std::string cs_path{};        /**< The dotted C# path (`Outer.Inner`). */
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
        const char* name;
        const char* sig;
        std::size_t k;
    };
    std::vector<vslot> vslots{};
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
        cs_path = std::move(o.cs_path);
        sink = o.sink;
        doc_text = std::move(o.doc_text);
        cpp_qualified = std::move(o.cpp_qualified);
        sym_prefix = std::move(o.sym_prefix);
        destroy_symbol = std::move(o.destroy_symbol);
        handle_field = std::move(o.handle_field);
        handle_cs = std::move(o.handle_cs);
        base_ref = std::move(o.base_ref);
        base_upcast_sym = std::move(o.base_upcast_sym);
        is_director = o.is_director;
        director_ident = std::move(o.director_ident);
        vslots = std::move(o.vslots);
        members = std::move(o.members);
        comparisons = std::move(o.comparisons);
        indexer_sigs = std::move(o.indexer_sigs);
        o.doc = nullptr;
        return *this;
    }

    /** Whether a comparison with this exact shape was recorded. */
    bool _have_comparison(std::string_view op, std::string_view lhs,
                          std::string_view rhs) const {
        for (const auto& c : comparisons)
            if (c.op == op && c.lhs == lhs && c.rhs == rhs)
                return true;
        return false;
    }

    /** Render the recorded comparisons with C#'s pairing rules applied. */
    std::string _flush_comparisons() const {
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
            const bool can_pair{_have_comparison(partner(c.op), c.lhs, c.rhs) ||
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
            if (_have_comparison(partner(c.op), c.lhs, c.rhs))
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
        if (_have_comparison("==", self_ph, self_ph)) {
            out += "        public override bool Equals(object? obj) => obj is " +
                   cs_name + " __o && this == __o;\n";
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
        std::string& out{sink ? *sink : doc->types};
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
            out += "        internal object? __owner;\n";
            // Whether this instance was constructed from C# (a director): its
            // virtual-slot methods then take the qualified base-call path.
            out += "        internal bool __isDirector;\n";
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
        out += members;
        out += _flush_comparisons();
        if (base_ref.empty())
            out += "        public virtual void Dispose() => " + handle_field +
                   ".Dispose();\n";
        else
            out += "        public override void Dispose() { " + handle_field +
                   ".Dispose(); base.Dispose(); }\n";
        out += "    }\n\n";
    }
};

/** An enum handle: a C# `enum : <underlying>` accumulated by `add_enumerator`,
    flushed by RAII (same rationale as @ref class_writer). No native thunks — an
    enum crosses as its underlying value. */
struct enum_writer {
    document* doc{nullptr};
    std::string* sink{nullptr}; /**< An outer class's members for a nested enum. */
    std::string cs_name{};
    std::string doc_text{};
    std::string underlying{}; /**< The C# underlying-type spelling (`int`, …). */
    std::string values{};     /**< Accumulated `        Name = value,` lines. */

    enum_writer() = default;
    enum_writer(const enum_writer&) = delete;
    enum_writer& operator=(const enum_writer&) = delete;
    enum_writer(enum_writer&& o) noexcept { *this = std::move(o); }
    enum_writer& operator=(enum_writer&& o) noexcept {
        doc = o.doc;
        sink = o.sink;
        cs_name = std::move(o.cs_name);
        doc_text = std::move(o.doc_text);
        underlying = std::move(o.underlying);
        values = std::move(o.values);
        o.doc = nullptr;
        return *this;
    }
    ~enum_writer() {
        if (!doc)
            return;
        std::string& out{sink ? *sink : doc->types};
        emit_doc_comment(out, "    ", doc_text.empty() ? nullptr : doc_text.c_str());
        out += "    public enum " + cs_name + " : " + underlying + "\n    {\n";
        out += values;
        out += "    }\n\n";
    }
};

} // namespace welder::inline v0::rods::csharp
