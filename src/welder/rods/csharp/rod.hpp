#pragma once
/** @file
    welder C#/.NET backend (header-only, text-emitting).

    C# has no in-process "register a class into a live module" C API the way CPython
    and Lua do, so this backend cannot register at C++ load time like the pybind11 /
    sol2 rods. Instead it takes the idiomatic, **cross-platform** interop path — a C
    ABI shared library driven from managed code by P/Invoke — and, like the LuaCATS
    stub rod, emits it as *text* at build time. Uniquely, it emits **two coordinated
    artifacts** from one driver pass:

    - `shim.cpp` — `extern "C"` thunks compiled **with reflection enabled** against
      the same welded header, each a one-liner delegating into the compiled
      marshalling library (`<welder/rods/csharp/shim_support.hpp>`) parameterized by
      the exact member reflection — the trampolines rod's splice-don't-respell idiom
      applied to the C ABI, so no C++ type is ever respelled as text;
    - `Bindings.cs` — `[LibraryImport]` P/Invoke declarations plus idiomatic wrapper
      classes (a per-class `SafeHandle`, `IDisposable`, properties, natural C#
      overloads, `enum : <underlying>`), calling those thunks.

    Both are written together per emission primitive, keyed by the same symbol, so
    the native and managed sides cannot desync (a colliding symbol is a designed
    generator error). Every thunk carries a trailing `welder_error*` out-param: a
    C++ exception is caught in the marshalling layer and rethrown managed-side as
    `WelderNativeException` — it never unwinds through the C ABI.

    This plugs the *same* generic driver (`<welder/welder.hpp>`) the other rods use,
    so member selection, overload grouping, policy / mark resolution and the
    bindability gate are reused verbatim — only the emission differs. Overload
    groups map to natural C# overloading with per-overload symbols.

    Phase-1 scope: classes (data fields → properties, default / parameterized /
    aggregate constructors, instance & static methods with overloads, method-backed
    properties, `Clone()` for the admitted copy constructor), enums
    (`enum : <underlying>`, per-enumerator docs), free functions, namespace
    variables and submodules; scalar / string / welded-class / enum parameters and
    returns; full XML doc comments. Deferred to later phases (a designed
    generation-time error, never silent): operators / comparisons / stringifier,
    welded-base inheritance, virtual dispatch overridden in C# (directors),
    reference-semantics ownership (`rv::` mapping) and container marshalling.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`).
    Drive it through the whole-module generator:
    @code
    #include <welder/rods/csharp/module.hpp>
    WELDER_CSHARP_MAIN(mymod, "mymod.hpp", "mymod_native")
    @endcode
*/

#include <cstddef>
#include <meta>
#include <ostream>
#include <string>
#include <utility>

#include <welder/welder.hpp>               // welder::welder + rod contract + driver
#include <welder/naming.hpp>               // name_of + ent_kind + restyle
#include <welder/bind_traits.hpp>          // param_types / aggregate_fields
#include <welder/doc.hpp>                  // doc_of / param_docs / return_doc_of
#include <welder/rods/csharp/document.hpp> // the two-stream document + writers
#include <welder/rods/csharp/type_map.hpp> // classify / spellings / lookup layer
#include <welder/rods/csharp/naming.hpp>   // the dotnet name style

namespace welder::inline v0::rods::csharp {

// A gcc-16 note that shapes this whole section: a `consteval` call whose argument is
// a *local* `constexpr` (not a template parameter) is rejected in a runtime
// expression ("consteval-only expressions are only allowed in a constant-evaluated
// context"), and a `consteval` call sitting in a *not-taken* `switch` branch is still
// evaluated at instantiation (so `qualified_cpp_name(int)` would throw). Both are
// avoided the same way: route every reflection-derived name through a **variable
// template** (forcing constant initialization) and branch with **`if constexpr`** on
// the compile-time `classify(...)` so only the taken branch instantiates.

/** The `::`-qualified C++ name of class/enum/namespace @a T as a runtime-usable
    static C string. Only instantiated for entities with a spellable path. */
template <std::meta::info T>
inline constexpr const char* cpp_name_v =
    std::define_static_string(qualified_cpp_name(T));

/** The styled C# name of entity @a E (kind @a K) under name style @a Style. */
template <std::meta::info E, class Style, ::welder::ent_kind K>
inline constexpr const char* styled_v = ::welder::name_of<E, lang::cs, Style, K>();

/** The `welder_<underscore-path>` C-symbol prefix of namespace/type @a Ent. */
template <std::meta::info Ent>
inline constexpr const char* upath_v =
    std::define_static_string(underscore_path(Ent));

/** The fixed-width C ABI wire spelling the shim signature uses for @a type.
    Marshallability was already enforced (@ref require_marshallable) by the
    caller, so `unsupported` cannot reach this. */
consteval const char* shim_wire_spelling(std::meta::info type, bool is_return) {
    switch (classify(type)) {
        case marshal_kind::void_:       return "void";
        case marshal_kind::scalar:      return scalar_spell(type).c_abi;
        case marshal_kind::boolean:     return "bool";
        case marshal_kind::utf8_string: return is_return ? "const char*"
                                                         : "const char*";
        case marshal_kind::enum_:       return enum_wire_spell(type).c_abi;
        default:                        return "void*"; // handle
    }
}

/** @ref shim_wire_spelling as constant-initialized variable templates. */
template <std::meta::info T>
inline constexpr const char* wire_param_v = shim_wire_spelling(T, false);
template <std::meta::info T>
inline constexpr const char* wire_return_v = shim_wire_spelling(T, true);

/** The camelCase C# parameter identifier for reflection @a param (falling back
    to `p<j>` for an unnamed parameter). */
consteval std::string param_ident(std::meta::info param, std::size_t j) {
    if (std::meta::has_identifier(param))
        return ::welder::naming::restyle(std::meta::identifier_of(param),
                                         ::welder::naming::case_kind::camel);
    std::string s{"p"};
    s += static_cast<char>('0' + (j / 10) % 10);
    s += static_cast<char>('0' + j % 10);
    return j < 10 ? "p" + std::string{static_cast<char>('0' + j)} : s;
}

/** Escape a C# keyword with the verbatim-identifier prefix (`@int`), so a C++
    parameter named `string` or `base` still yields legal C#. */
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

// --- per-type marshalling spellings -----------------------------------------

/** The managed type the P/Invoke declaration uses for @a Type. @a is_return
    switches a string between its `in` (`string`) and `out` (`IntPtr`,
    caller-freed) forms; a welded-class parameter is typed as its `SafeHandle`
    subclass (premature-collection safety on the call), a return as `IntPtr`. */
template <std::meta::info Type, class Style>
std::string pinvoke_type(bool is_return) {
    constexpr marshal_kind k{classify(Type)};
    if constexpr (k == marshal_kind::void_) return "void";
    else if constexpr (k == marshal_kind::scalar) {
        constexpr const char* s{scalar_spell(Type).cs};
        return s;
    } else if constexpr (k == marshal_kind::boolean) return "bool";
    else if constexpr (k == marshal_kind::utf8_string)
        return is_return ? "IntPtr" : "string";
    else if constexpr (k == marshal_kind::enum_)
        return std::string{styled_v<bare(Type), Style, ::welder::ent_kind::enum_>};
    else // handle
        return is_return
                   ? std::string{"IntPtr"}
                   : std::string{
                         styled_v<bare(Type), Style, ::welder::ent_kind::class_>} +
                         "Handle";
}

/** The public C# type the wrapper API exposes for @a Type. */
template <std::meta::info Type, class Style>
std::string public_type() {
    constexpr marshal_kind k{classify(Type)};
    if constexpr (k == marshal_kind::void_) return "void";
    else if constexpr (k == marshal_kind::scalar) {
        constexpr const char* s{scalar_spell(Type).cs};
        return s;
    } else if constexpr (k == marshal_kind::boolean) return "bool";
    else if constexpr (k == marshal_kind::utf8_string) return "string";
    else if constexpr (k == marshal_kind::enum_)
        return std::string{styled_v<bare(Type), Style, ::welder::ent_kind::enum_>};
    else
        return std::string{styled_v<bare(Type), Style, ::welder::ent_kind::class_>};
}

/** The public C# RETURN type: like @ref public_type, plus the `?` nullable
    marker for a pointer-flavor welded-class return (a C++ `nullptr` maps to
    C# `null`). */
template <std::meta::info R, class Style>
std::string public_return_type() {
    if constexpr (classify(R) == marshal_kind::handle) {
        if constexpr (handle_return_nullable(R))
            return public_type<R, Style>() + "?";
        else
            return public_type<R, Style>();
    } else {
        return public_type<R, Style>();
    }
}

/** The wrapper statements converting the checked P/Invoke result into the
    managed return value. @a pc is the P/Invoke call expression (without the
    trailing error arg — appended here so every call is checked), @a ind the
    indentation. A welded-class result follows @ref handle_return_of for
    policy @a Rv: owned kinds wrap with `owns: true`; view kinds with
    `owns: false`, and `view_keepalive` additionally stores @a owner in the
    view's `__owner` (preventing collection of the parent while the view
    lives — the managed spelling of `reference_internal`). */
template <std::meta::info R, class Style,
          ::welder::rv_kind Rv = ::welder::rv_kind::automatic>
std::string wrapper_return_body(const std::string& pc, const std::string& ind,
                                const std::string& owner = {}) {
    constexpr marshal_kind k{classify(R)};
    const std::string check{ind + "WelderInterop.ThrowIfError(in __e);\n"};
    if constexpr (k == marshal_kind::void_)
        return ind + pc + ";\n" + check;
    else if constexpr (k == marshal_kind::utf8_string)
        return ind + "IntPtr __r = " + pc + ";\n" + check +
               ind + "try { return Marshal.PtrToStringUTF8(__r) ?? \"\"; }\n" +
               ind + "finally { NativeMethods.welder_free(__r); }\n";
    else if constexpr (k == marshal_kind::handle) {
        constexpr handle_return hr{handle_return_of(R, Rv)};
        std::string out{ind + "IntPtr __r = " + pc + ";\n" + check};
        if constexpr (handle_return_nullable(R))
            out += ind + "if (__r == IntPtr.Zero) return null;\n";
        if constexpr (hr == handle_return::view ||
                      hr == handle_return::view_keepalive) {
            out += ind + "var __v = new " + public_type<R, Style>() +
                   "(__r, false);\n";
            if (hr == handle_return::view_keepalive && !owner.empty())
                out += ind + "__v.__owner = " + owner + ";\n";
            out += ind + "return __v;\n";
        } else {
            out += ind + "return new " + public_type<R, Style>() +
                   "(__r, true);\n";
        }
        return out;
    } else {
        return ind + "var __r = " + pc + ";\n" + check + ind + "return __r;\n";
    }
}

/** The policy a data member's read binds under: a non-const welded-class
    member hands out a live view tied to its parent (the runtime rods'
    `def_readwrite` reference_internal semantics); everything else crosses by
    value. Mirrored structurally by `shim::field_get`. */
consteval ::welder::rv_kind field_return_policy(std::meta::info MT) {
    return (classify(MT) == marshal_kind::handle &&
            !std::meta::is_const_type(MT))
               ? ::welder::rv_kind::reference_internal
               : ::welder::rv_kind::automatic;
}

/** The per-parameter string lists a callable needs, built together. */
struct call_pieces {
    std::string shim_params{};    /**< `std::int32_t a0, const char* a1`. */
    std::string delegate_args{};  /**< `a0, a1` — handed to the support template. */
    std::string pinvoke_params{}; /**< the P/Invoke parameter list. */
    std::string wrapper_params{}; /**< the public wrapper parameter list. */
    std::string wrapper_args{};   /**< the wrapper→P/Invoke argument expressions. */
    std::string param_names{};    /**< `\x1f`-joined C# names (XML `<param>` keys). */
    bool has_string{false};       /**< any UTF-8 string ⇒ the Utf8 attribute variant. */
};

/** Append one parameter (C++ type @a PT, position @a j, C# name @a csname) to
    @a cp. Shared by the function-parameter and aggregate-field paths. */
template <std::meta::info PT, class Style>
void append_one_param(call_pieces& cp, std::size_t j, const char* csname) {
    // Marshallability is enforced here — once per parameter, loudly.
    constexpr bool checked{(require_marshallable(PT, false), true)};
    static_assert(checked);
    const std::string i{std::to_string(j)};
    const std::string name{cs_escape(csname)};
    if (j != 0) {
        cp.shim_params += ", ";
        cp.delegate_args += ", ";
        cp.pinvoke_params += ", ";
        cp.wrapper_params += ", ";
        cp.wrapper_args += ", ";
    }
    constexpr const char* abi{wire_param_v<PT>};
    cp.shim_params += std::string{abi} + " a" + i;
    cp.delegate_args += "a" + i;
    if constexpr (classify(PT) == marshal_kind::boolean)
        cp.pinvoke_params += "[MarshalAs(UnmanagedType.U1)] ";
    cp.pinvoke_params += pinvoke_type<PT, Style>(false) + " a" + i;
    cp.wrapper_params += public_type<PT, Style>() + " " + name;
    if constexpr (classify(PT) == marshal_kind::handle)
        cp.wrapper_args += name + "._handle";
    else
        cp.wrapper_args += name;
    cp.param_names += name + '\x1f';
    if constexpr (classify(PT) == marshal_kind::utf8_string)
        cp.has_string = true;
}

/** Build the @ref call_pieces for callable @a Fn's parameters (a flat function
    template + constant-index pack — the gcc-16 workaround luacats also uses). */
template <std::meta::info Fn, class Style, std::size_t... I>
call_pieces build_params(std::index_sequence<I...>) {
    call_pieces cp{};
    // Guard the empty pack: param_types<Fn> materializes std::array<info, 0> and
    // indexing that is ill-formed, so it must not be instantiated for a
    // parameterless callable (same guard luacats' param_lua_types uses).
    if constexpr (sizeof...(I) != 0) {
        constexpr auto pts = ::welder::detail::param_types<Fn>();
        static constexpr const char* names[]{std::define_static_string(
            param_ident(std::meta::parameters_of(Fn)[I], I))...};
        (append_one_param<pts[I], Style>(cp, I, names[I]), ...);
    }
    return cp;
}

/** Build the @ref call_pieces for aggregate @a T's fields (parenthesized aggregate
    construction). */
template <class T, class Style, std::size_t... J>
call_pieces aggregate_pieces(std::index_sequence<J...>) {
    call_pieces cp{};
    if constexpr (sizeof...(J) != 0) {
        constexpr auto fields = ::welder::detail::aggregate_fields<T>();
        static constexpr const char* names[]{
            std::define_static_string(param_ident(fields[J], J))...};
        (append_one_param<std::meta::type_of(fields[J]), Style>(cp, J, names[J]),
         ...);
    }
    return cp;
}

/** The `[LibraryImport(Lib…)]` attribute, adding UTF-8 string marshalling when
    @a has_string. */
inline std::string import_attr(bool has_string) {
    return has_string
               ? "[LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]"
               : "[LibraryImport(Lib)]";
}

/** Split @a cp.param_names back into its `\x1f`-separated pieces. */
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

/** Emit the XML doc block for callable @a Fn above its wrapper: `<summary>` from
    its `[[=welder::doc]]`, one `<param>` per documented parameter (keyed by the
    C# parameter names in @a cp) and `<returns>` from `[[=welder::returns]]`. */
template <std::meta::info Fn>
void emit_callable_docs(std::string& out, const std::string& ind,
                        const call_pieces& cp) {
    emit_doc_comment(out, ind, ::welder::doc_of<Fn>());
    static constexpr auto pds{::welder::param_docs<Fn>()};
    if constexpr (pds.size() != 0) {
        const std::vector<std::string> names{split_param_names(cp.param_names)};
        for (std::size_t i{0}; i < pds.size(); ++i) {
            if (!pds[i].text || !*pds[i].text)
                continue;
            const std::string name{
                i < names.size()
                    ? names[i]
                    : std::string{pds[i].name ? pds[i].name : ""}};
            out += ind + "/// <param name=\"" + name + "\">" +
                   one_line(pds[i].text) + "</param>\n";
        }
    }
    if (const char* r{::welder::return_doc_of<Fn>()}; r && *r)
        out += ind + "/// <returns>" + one_line(r) + "</returns>\n";
}

/** Emit one callable overload: its shim thunk (a one-line delegation into
    `shim_support.hpp`, into `doc.shim`), its P/Invoke declaration (into
    `doc.pinvoke`) and its wrapper (into @a wrapper_out).

    @tparam HasSelf true for an instance entity (a leading `void* self` /
                    `_handle`); false for a static method or free function.
    @param delegate_expr the support-template instantiation text, e.g.
           `wcs::shim::method<^^::geo::Point, wcs::named_member(^^::geo::Point,
           "area", 0)>` — the thunk body appends `(self, err, a0, …)`. */
template <std::meta::info Fn, class Style, bool HasSelf>
void emit_callable(document& doc, const std::string& sym, std::string& wrapper_out,
                   const std::string& indent, const std::string& wrapper_name,
                   const std::string& delegate_expr,
                   const std::string& self_cs = {}) {
    ::welder::validate_return_policy<Fn, lang::cs>();
    constexpr std::meta::info R{std::meta::return_type_of(Fn)};
    constexpr bool ret_checked{(require_marshallable(R, true), true)};
    static_assert(ret_checked);
    constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
    const call_pieces cp{build_params<Fn, Style>(std::make_index_sequence<n>{})};
    doc.record_symbol(sym);

    // --- shim thunk (a delegation one-liner) --------------------------------
    std::string shim_params{cp.shim_params};
    if constexpr (HasSelf)
        shim_params = "void* self" +
                      (cp.shim_params.empty() ? "" : ", " + cp.shim_params);
    shim_params += (shim_params.empty() ? "" : ", ");
    shim_params += "welder_error* err";
    std::string delegate_args{HasSelf ? "self, err" : "err"};
    if (!cp.delegate_args.empty())
        delegate_args += ", " + cp.delegate_args;
    doc.shim += std::string{wire_return_v<std::meta::return_type_of(Fn)>} + " " + sym + "(" + shim_params +
                ") { return " + delegate_expr + "(" + delegate_args + "); }\n\n";

    // --- P/Invoke declaration ----------------------------------------------
    std::string pin_params{cp.pinvoke_params};
    if constexpr (HasSelf)
        pin_params = self_cs + " self" +
                     (cp.pinvoke_params.empty() ? "" : ", " + cp.pinvoke_params);
    pin_params += (pin_params.empty() ? "" : ", ");
    pin_params += "out WelderError err";
    constexpr bool r_is_bool{classify(R) == marshal_kind::boolean};
    const std::string ret_attr{r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] "
                                         : ""};
    doc.pinvoke += "        " + import_attr(cp.has_string) + " " + ret_attr +
                   "internal static partial " +
                   pinvoke_type<std::meta::return_type_of(Fn), Style>(true) + " " +
                   sym + "(" + pin_params + ");\n";

    // --- managed wrapper ----------------------------------------------------
    std::string call_args{HasSelf ? "_handle" : ""};
    if (!cp.wrapper_args.empty())
        call_args += (call_args.empty() ? "" : ", ") + cp.wrapper_args;
    call_args += (call_args.empty() ? "" : ", ");
    call_args += "out WelderError __e";
    const std::string pc{"NativeMethods." + sym + "(" + call_args + ")"};
    emit_callable_docs<Fn>(wrapper_out, indent, cp);
    wrapper_out += indent + "public " + (HasSelf ? "" : "static ") +
                   public_return_type<std::meta::return_type_of(Fn), Style>() +
                   " " + wrapper_name + "(" + cp.wrapper_params + ")\n" + indent +
                   "{\n";
    wrapper_out += wrapper_return_body<std::meta::return_type_of(Fn), Style,
                                       ::welder::return_policy_of(Fn, lang::cs)>(
        pc, indent + "    ", HasSelf ? "this" : "");
    wrapper_out += indent + "}\n\n";
}

/** Emit a constructor from its parameter pieces: the shim delegation, an
    `IntPtr` P/Invoke, and a `public T(...)` wrapper. */
inline void emit_ctor(class_writer& w, const call_pieces& cp,
                      const std::string& sym, const std::string& delegate_expr) {
    w.doc->record_symbol(sym);
    std::string shim_params{cp.shim_params};
    shim_params += (shim_params.empty() ? "" : ", ");
    shim_params += "welder_error* err";
    std::string delegate_args{"err"};
    if (!cp.delegate_args.empty())
        delegate_args += ", " + cp.delegate_args;
    w.doc->shim += "void* " + sym + "(" + shim_params + ") { return " +
                   delegate_expr + "(" + delegate_args + "); }\n\n";
    std::string pin_params{cp.pinvoke_params};
    pin_params += (pin_params.empty() ? "" : ", ");
    pin_params += "out WelderError err";
    w.doc->pinvoke += "        " + import_attr(cp.has_string) +
                      " internal static partial IntPtr " + sym + "(" +
                      pin_params + ");\n";
    w.members += "        public " + w.cs_name + "(" + cp.wrapper_params + ")\n" +
                 "        {\n            IntPtr __r = NativeMethods." + sym + "(" +
                 (cp.wrapper_args.empty() ? std::string{}
                                          : cp.wrapper_args + ", ") +
                 "out WelderError __e);\n"
                 "            WelderInterop.ThrowIfError(in __e);\n"
                 "            _handle = new " + w.cs_name + "Handle(__r, true);\n"
                 "        }\n\n";
}

/** The C#/.NET rod: a stateless policy satisfying @ref welder::rod that emits a
    native shim + a managed P/Invoke wrapper instead of registering a live module. */
struct rod {
    static constexpr lang language{lang::cs};
    using module_type = module_writer;

    /** The class / enum handles the per-class / per-enum hooks write into —
        exactly what `make_class` / `make_enum` return. Named as associated types
        so the @ref welder::rod concept can shape-check the per-handle hooks. */
    template <class> using class_handle_type = class_writer;
    template <class> using enum_handle_type = enum_writer;

    struct session {}; /**< No deferred module state. */

    /** @ref is_native_dotnet drives the shared bindability gate. */
    template <class T>
    static constexpr bool has_native_caster = is_native_dotnet<std::remove_cvref_t<T>>;

    /** Operators are deferred until the operator phase: returning `nullptr`
        disables operator binding (the driver's eligibility gate), like an
        unmapped operator elsewhere. */
    static consteval const char* special_method_name(std::meta::info) {
        return nullptr;
    }

    // --- class binding ------------------------------------------------------

    template <class T, auto Bases, std::size_t... I>
    static class_writer make_class(module_type& m, const char* name,
                                   const char* doc, std::index_sequence<I...> seq) {
        return make_class<T, ^^T, Bases>(m, name, doc, seq);
    }

    /** The declaring-entity-aware form the carriage prefers: @a Decl is `^^T`,
        or the namespace-scope **alias** a class-template specialization was
        welded through — the one C++-spellable anchor such a target has, which
        the emitted shim's `^^…` spellings need. */
    template <class T, std::meta::info Decl, auto Bases, std::size_t... I>
    static class_writer make_class(module_type& m, const char* name,
                                   const char* doc, std::index_sequence<I...>) {
        static_assert(sizeof...(I) == 0,
                      "welder: the C# rod does not bind welded base classes yet "
                      "(cross-boundary inheritance lands in a later phase); "
                      "mark::exclude the derived type for lang::cs, or drop the "
                      "base's weld for cs");
        class_writer w{};
        w.doc = m.doc;
        w.cs_name = name;
        w.doc_text = doc ? doc : "";
        w.cpp_qualified = cpp_name_v<Decl>;
        w.sym_prefix = std::string{"welder_"} + upath_v<Decl>;
        w.destroy_symbol = w.sym_prefix + "_destroy";
        // The destructor thunk + its P/Invoke (the SafeHandle's release path).
        m.doc->record_symbol(w.destroy_symbol);
        m.doc->shim += "void " + w.destroy_symbol +
                       "(void* self) { wcs::shim::destroy<^^" + w.cpp_qualified +
                       ">(self); }\n\n";
        m.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial void " +
                          w.destroy_symbol + "(IntPtr self);\n";
        return w;
    }

    /** Emit the whole constructor surface in one call (main's contract): a
        no-argument form when @a HasDefault, one per member of @a Ctors (exact
        constructors, spliced via `ctor_at`), the aggregate field constructor
        when @a Aggregate, and — @a Copyable — the admitted copy constructor as
        the managed `Clone()` (C# has no copy-constructor protocol; a `T(other)`
        overload would collide with a one-argument user constructor). */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate, bool Copyable>
    static void add_constructors(class_writer& w) {
        const std::string anchor{"^^" + w.cpp_qualified};
        if constexpr (HasDefault) {
            emit_ctor(w, call_pieces{}, w.sym_prefix + "_new_default",
                      "wcs::shim::default_construct<" + anchor + ">");
        }
        template for (constexpr auto ctor : std::define_static_array(Ctors)) {
            constexpr std::size_t k{index_of_ctor(ctor)};
            constexpr std::size_t n{std::meta::parameters_of(ctor).size()};
            emit_ctor(w,
                      build_params<ctor, ::welder::naming::none>(
                          std::make_index_sequence<n>{}),
                      w.sym_prefix + "_new_" + std::to_string(k),
                      "wcs::shim::construct<" + anchor + ", wcs::ctor_at(" +
                          anchor + ", " + std::to_string(k) + ")>");
        }
        if constexpr (Aggregate) {
            constexpr std::size_t n{
                ::welder::detail::aggregate_fields<T>().size()};
            emit_ctor(w,
                      aggregate_pieces<T, ::welder::naming::none>(
                          std::make_index_sequence<n>{}),
                      w.sym_prefix + "_new_agg",
                      "wcs::shim::aggregate_construct<" + anchor + ">");
        }
        if constexpr (Copyable) {
            const std::string sym{w.sym_prefix + "_clone"};
            w.doc->record_symbol(sym);
            w.doc->shim += "void* " + sym +
                           "(void* self, welder_error* err) { return "
                           "wcs::shim::clone<" + anchor + ">(self, err); }\n\n";
            w.doc->pinvoke += "        [LibraryImport(Lib)] internal static "
                              "partial IntPtr " + sym + "(" + w.cs_name +
                              "Handle self, out WelderError err);\n";
            w.members += "        /// <summary>Copy this instance (the C++ copy "
                         "constructor).</summary>\n"
                         "        public " + w.cs_name + " Clone()\n"
                         "        {\n"
                         "            IntPtr __r = NativeMethods." + sym +
                         "(_handle, out WelderError __e);\n"
                         "            WelderInterop.ThrowIfError(in __e);\n"
                         "            return new " + w.cs_name + "(__r, true);\n"
                         "        }\n\n";
        }
    }

    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(class_writer& w) {
        constexpr std::meta::info MT{std::meta::type_of(Mem)};
        constexpr bool checked{(require_marshallable(MT, true), true)};
        static_assert(checked);
        const std::string id{std::meta::identifier_of(Mem)};
        const std::string anchor{"^^" + w.cpp_qualified};
        // The lookup is by the DECLARING scope (a flattened non-welded base's
        // member is not among the welded type's own members); the invocation
        // object stays the welded type — the pointer-to-member converts.
        const std::string owner{"^^" +
                                std::string{cpp_name_v<std::meta::parent_of(Mem)>}};
        const std::string lookup{"wcs::named_field(" + owner + ", \"" + id +
                                 "\")"};
        const std::string getsym{w.sym_prefix + "_get_" + id};
        const std::string setsym{w.sym_prefix + "_set_" + id};
        constexpr bool read_only{std::meta::is_const_type(MT) ||
                                 ::welder::member_no_reassign(Mem, language)};
        constexpr bool is_str{classify(MT) == marshal_kind::utf8_string};
        constexpr bool is_bool{classify(MT) == marshal_kind::boolean};
        constexpr bool is_handle{classify(MT) == marshal_kind::handle};

        // getter thunk + P/Invoke
        w.doc->record_symbol(getsym);
        w.doc->shim += std::string{wire_return_v<std::meta::type_of(Mem)>} +
                       " " + getsym +
                       "(void* self, welder_error* err) { return "
                       "wcs::shim::field_get<" + anchor + ", " + lookup +
                       ">(self, err); }\n\n";
        w.doc->pinvoke += "        " + import_attr(false) + " " +
                          (is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "") +
                          "internal static partial " +
                          pinvoke_type<std::meta::type_of(Mem), Style>(true) +
                          " " + getsym + "(" + w.cs_name +
                          "Handle self, out WelderError err);\n";
        if constexpr (!read_only) {
            w.doc->record_symbol(setsym);
            w.doc->shim += "void " + setsym + "(void* self, " +
                           std::string{wire_param_v<std::meta::type_of(Mem)>} +
                           " v, welder_error* err) { return "
                           "wcs::shim::field_set<" + anchor + ", " + lookup +
                           ">(self, err, v); }\n\n";
            w.doc->pinvoke += "        " + import_attr(is_str) +
                              " internal static partial void " + setsym +
                              "(" + w.cs_name + "Handle self, " +
                              (is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
                              pinvoke_type<std::meta::type_of(Mem), Style>(false) +
                              " v, out WelderError err);\n";
        }

        // property
        const std::string pname{
            ::welder::name_of<Mem, lang::cs, Style, ::welder::ent_kind::field>()};
        emit_doc_comment(w.members, "        ", ::welder::doc_of<Mem>());
        w.members += "        public " +
                     public_type<std::meta::type_of(Mem), Style>() + " " + pname +
                     "\n        {\n            get\n            {\n";
        w.members += wrapper_return_body<std::meta::type_of(Mem), Style,
                                         field_return_policy(
                                             std::meta::type_of(Mem))>(
            "NativeMethods." + getsym + "(_handle, out WelderError __e)",
            "                ", "this");
        w.members += "            }\n";
        if constexpr (!read_only) {
            w.members += "            set\n            {\n"
                         "                NativeMethods." + setsym + "(_handle, " +
                         (is_handle ? "value._handle" : "value") +
                         ", out WelderError __e);\n"
                         "                WelderInterop.ThrowIfError(in __e);\n"
                         "            }\n";
        }
        w.members += "        }\n\n";
    }

    /** One resolved method-backed property: the getter's thunk under a
        property-read symbol, the (optional) setter's under a write symbol, and
        a C# property calling them under the driver-resolved @a name. */
    template <class T, std::meta::info Getter, std::meta::info Setter>
    static void add_property(class_writer& w, const char* name) {
        ::welder::validate_return_policy<Getter, lang::cs>();
        constexpr std::meta::info RT{
            std::meta::remove_cvref(std::meta::return_type_of(Getter))};
        constexpr bool checked{(require_marshallable(RT, true), true)};
        static_assert(checked);
        const std::string anchor{"^^" + w.cpp_qualified};
        const std::string gid{std::meta::identifier_of(Getter)};
        const std::string glookup{
            "wcs::named_member(^^" +
            std::string{cpp_name_v<std::meta::parent_of(Getter)>} + ", \"" +
            gid + "\", " + std::to_string(index_of_named_member(Getter)) + ")"};
        const std::string getsym{w.sym_prefix + "_pget_" + name};
        w.doc->record_symbol(getsym);
        w.doc->shim += std::string{wire_return_v<std::meta::remove_cvref(std::meta::return_type_of(Getter))>} + " " + getsym +
                       "(void* self, welder_error* err) { return "
                       "wcs::shim::method<" + anchor + ", " + glookup +
                       ">(self, err); }\n\n";
        constexpr bool is_bool{classify(RT) == marshal_kind::boolean};
        w.doc->pinvoke += "        " + import_attr(false) + " " +
                          (is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "") +
                          "internal static partial " +
                          pinvoke_type<std::meta::remove_cvref(std::meta::return_type_of(Getter)), ::welder::naming::none>(true) +
                          " " + getsym + "(" + w.cs_name +
                          "Handle self, out WelderError err);\n";

        emit_doc_comment(w.members, "        ", ::welder::doc_of<Getter>());
        w.members += "        public " +
                     public_type<std::meta::remove_cvref(std::meta::return_type_of(Getter)), ::welder::naming::none>() +
                     " " + name + "\n        {\n            get\n            {\n";
        w.members += wrapper_return_body<std::meta::return_type_of(Getter),
                                         ::welder::naming::none,
                                         ::welder::return_policy_of(
                                             Getter, lang::cs)>(
            "NativeMethods." + getsym + "(_handle, out WelderError __e)",
            "                ", "this");
        w.members += "            }\n";
        if constexpr (Setter != std::meta::info{}) {
            constexpr std::meta::info PT{
                ::welder::detail::param_types<Setter>()[0]};
            constexpr bool pchecked{(require_marshallable(PT, false), true)};
            static_assert(pchecked);
            const std::string sid{std::meta::identifier_of(Setter)};
            const std::string slookup{
                "wcs::named_member(^^" +
                std::string{cpp_name_v<std::meta::parent_of(Setter)>} +
                ", \"" + sid + "\", " +
                std::to_string(index_of_named_member(Setter)) + ")"};
            const std::string setsym{w.sym_prefix + "_pset_" + name};
            w.doc->record_symbol(setsym);
            // A fluent (non-void) setter return is discarded by shim::method's
            // guarded<void> path? No — the support template returns the wire
            // form of the REAL return type; the thunk discards it by spelling
            // void and an expression statement.
            w.doc->shim += "void " + setsym + "(void* self, " +
                           std::string{wire_param_v<first_param_type(Setter)>} +
                           " v, welder_error* err) { (void)wcs::shim::method<" +
                           anchor + ", " + slookup + ">(self, err, v); }\n\n";
            constexpr bool p_is_bool{classify(PT) == marshal_kind::boolean};
            constexpr bool p_is_str{classify(PT) == marshal_kind::utf8_string};
            constexpr bool p_is_handle{classify(PT) == marshal_kind::handle};
            w.doc->pinvoke += "        " + import_attr(p_is_str) +
                              " internal static partial void " + setsym +
                              "(" + w.cs_name + "Handle self, " +
                              (p_is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
                              pinvoke_type<first_param_type(Setter),
                                           ::welder::naming::none>(false) +
                              " v, out WelderError err);\n";
            w.members += "            set\n            {\n"
                         "                NativeMethods." + setsym + "(_handle, " +
                         (p_is_handle ? "value._handle" : "value") +
                         ", out WelderError __e);\n"
                         "                WelderInterop.ThrowIfError(in __e);\n"
                         "            }\n";
        }
        w.members += "        }\n\n";
    }

    /** Emit method overload group @a Fns as natural C# overloads sharing one
        name, each with its own indexed symbol (the group's name resolves from
        `Fns[0]`; the index re-derives the exact overload shim-side). */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_method(class_writer& w) {
        const std::string name{::welder::name_of<Fns[0], lang::cs, Style,
                                                 ::welder::ent_kind::method>()};
        const std::string anchor{"^^" + w.cpp_qualified};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            constexpr std::size_t k{index_of_named_member(fn)};
            const std::string id{std::meta::identifier_of(fn)};
            const std::string sym{w.sym_prefix + "_m_" + id + "_" +
                                  std::to_string(k)};
            const std::string expr{
                "wcs::shim::method<" + anchor + ", wcs::named_member(^^" +
                std::string{cpp_name_v<std::meta::parent_of(fn)>} + ", \"" + id +
                "\", " + std::to_string(k) + ")>"};
            emit_callable<fn, Style, true>(*w.doc, sym, w.members, "        ",
                                           name, expr, w.cs_name + "Handle");
        }
    }

    /** Emit static-method overload group @a Fns as `public static` overloads. */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_static_method(class_writer& w) {
        const std::string name{
            ::welder::name_of<Fns[0], lang::cs, Style,
                              ::welder::ent_kind::static_method>()};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            constexpr std::size_t k{index_of_named_member(fn)};
            const std::string id{std::meta::identifier_of(fn)};
            const std::string sym{w.sym_prefix + "_s_" + id + "_" +
                                  std::to_string(k)};
            const std::string expr{
                "wcs::shim::function<wcs::named_member(^^" +
                std::string{cpp_name_v<std::meta::parent_of(fn)>} + ", \"" + id +
                "\", " + std::to_string(k) + ")>"};
            emit_callable<fn, Style, false>(*w.doc, sym, w.members, "        ",
                                            name, expr);
        }
    }

    /** Operator slots are deferred until the operator phase (gated off by
        @ref special_method_name returning `nullptr`). */
    template <class T, auto Fns>
    static void add_operator(class_writer&) {}

    /** Comparison synthesis is deferred until the operator phase. */
    template <class T, auto Fns, auto Covered>
    static void add_comparisons(class_writer&) {}

    /** The ostream stringifier (`ToString()`) is deferred until the operator
        phase. */
    template <class T, std::meta::info Fn>
    static void add_stringifier(class_writer&) {}

    // --- enum binding -------------------------------------------------------

    template <class E>
    static enum_writer make_enum(module_type& m, const char* name,
                                 const char* doc) {
        enum_writer w{};
        w.doc = m.doc;
        w.cs_name = name;
        w.doc_text = doc ? doc : "";
        constexpr const char* u{scalar_spell(
            std::meta::underlying_type(std::meta::dealias(^^E))).cs};
        w.underlying = u;
        return w;
    }

    /** Emit a `Name = value,` line for enumerator @a Enum, preceded by its
        `[[=welder::doc]]` as a `<summary>` — C# has the per-member doc slot
        Python lacks, so nothing folds into the enum's summary. */
    template <std::meta::info Enum, class Style = ::welder::naming::none>
    static void add_enumerator(enum_writer& w) {
        emit_doc_comment(w.values, "        ", ::welder::doc_of<Enum>());
        w.values += "        ";
        w.values += ::welder::name_of<Enum, lang::cs, Style,
                                      ::welder::ent_kind::enumerator>();
        w.values += " = ";
        constexpr long long v{static_cast<long long>(std::to_underlying([:Enum:]))};
        w.values += std::to_string(v);
        w.values += ",\n";
    }

    template <class E>
    static void finish_enum(enum_writer&) {} // RAII flush handles it

    // --- namespace / module binding -----------------------------------------

    static session open_module(module_type&) { return {}; }

    /** The root namespace's doc becomes the `Bindings.cs` file-header comment
        (C# has no namespace doc slot); first caller wins — the root is swept
        first. */
    static void set_module_doc(module_type& m, const char* doc) {
        if (doc && *doc && m.doc->module_doc.empty())
            m.doc->module_doc = doc;
    }

    /** Emit free-function overload group @a Fns as `public static` overloads on
        the namespace's static class. A non-null @a name overrides the leaf name
        (beating any `weld_as`). */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_function(module_type& m, const char* name = nullptr) {
        const std::string wname{
            ::welder::name_of_or<Fns[0], lang::cs, Style,
                                 ::welder::ent_kind::function>(name)};
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            constexpr std::size_t k{index_of_named_member(fn)};
            constexpr std::meta::info Ns{std::meta::parent_of(fn)};
            const std::string id{std::meta::identifier_of(fn)};
            const std::string sym{std::string{"welder_"} + upath_v<Ns> + "_f_" +
                                  id + "_" + std::to_string(k)};
            const std::string expr{
                "wcs::shim::function<wcs::named_member(^^" +
                std::string{cpp_name_v<Ns>} + ", \"" + id + "\", " +
                std::to_string(k) + ")>"};
            emit_callable<fn, Style, false>(*m.doc, sym,
                                            m.doc->static_body(m.cs_class),
                                            "        ", wname, expr);
        }
    }

    template <std::meta::info Var, class Style = ::welder::naming::none>
    static void add_variable(module_type& m, session&, const char* name = nullptr) {
        constexpr std::meta::info VT{std::meta::type_of(Var)};
        constexpr bool checked{(require_marshallable(VT, true), true)};
        static_assert(checked);
        constexpr std::meta::info Ns{std::meta::parent_of(Var)};
        const std::string id{std::meta::identifier_of(Var)};
        const std::string lookup{"wcs::named_field(^^" +
                                 std::string{cpp_name_v<Ns>} + ", \"" + id +
                                 "\")"};
        const std::string base{std::string{"welder_"} + upath_v<Ns> + "_v_" + id};
        constexpr bool read_only{std::meta::is_const_type(VT)};
        constexpr bool is_str{classify(VT) == marshal_kind::utf8_string};
        constexpr bool is_bool{classify(VT) == marshal_kind::boolean};
        constexpr bool is_handle{classify(VT) == marshal_kind::handle};
        std::string& body{m.doc->static_body(m.cs_class)};

        m.doc->record_symbol(base + "_get");
        m.doc->shim += std::string{wire_return_v<std::meta::type_of(Var)>} + " " + base +
                       "_get(welder_error* err) { return wcs::shim::var_get<" +
                       lookup + ">(err); }\n\n";
        m.doc->pinvoke += "        " + import_attr(false) + " " +
                          (is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "") +
                          "internal static partial " +
                          pinvoke_type<std::meta::type_of(Var), Style>(true) + " " + base + "_get(out WelderError err);\n";
        if constexpr (!read_only) {
            m.doc->record_symbol(base + "_set");
            m.doc->shim += "void " + base + "_set(" +
                           std::string{wire_param_v<std::meta::type_of(Var)>} +
                           " v, welder_error* err) { return wcs::shim::var_set<" +
                           lookup + ">(err, v); }\n\n";
            m.doc->pinvoke += "        " + import_attr(is_str) +
                              " internal static partial void " + base + "_set(" +
                              (is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
                              pinvoke_type<std::meta::type_of(Var), Style>(false) +
                              " v, out WelderError err);\n";
        }
        const std::string vname{
            ::welder::name_of_or<Var, lang::cs, Style,
                                 ::welder::ent_kind::variable>(name)};
        emit_doc_comment(body, "        ", ::welder::doc_of<Var>());
        body += "        public static " + public_type<std::meta::type_of(Var), Style>() + " " +
                vname +
                "\n        {\n            get\n            {\n";
        body += wrapper_return_body<std::meta::type_of(Var), Style>(
            "NativeMethods." + base + "_get(out WelderError __e)",
            "                ");
        body += "            }\n";
        if constexpr (!read_only) {
            body += "            set\n            {\n"
                    "                NativeMethods." + base + "_set(" +
                    (is_handle ? "value._handle" : "value") +
                    ", out WelderError __e);\n"
                    "                WelderInterop.ThrowIfError(in __e);\n"
                    "            }\n";
        }
        body += "        }\n\n";
    }

    static module_type add_submodule(module_type& m, const char* name) {
        return module_type{m.doc, name};
    }

    static void close_module(module_type&, session&) {}

    // --- whole-module generation (this backend's extra entry point) ---------

    /** Emit the C# wrapper (@a cs) and native shim (@a shim) for namespace @a Ns.

        Runs welder's generic driver over @a Ns with this text-emitting backend, so
        the two artifacts cover exactly what would be bound at runtime — classes,
        enums, free functions, namespace variables and nested namespaces.
        @tparam Ns    a reflection of the (top-level) namespace / module.
        @tparam Style the C# name style (defaults to @ref dotnet).
        @param shim the stream for `shim.cpp`.
        @param cs   the stream for `Bindings.cs`.
        @param o    the module knobs (C# namespace, P/Invoke library, shim include). */
    template <std::meta::info Ns, class Style = dotnet>
    static void generate(std::ostream& shim, std::ostream& cs, options o) {
        static_assert(std::meta::is_namespace(Ns),
                      "welder: csharp::generate<Ns>: Ns must reflect a namespace");
        document doc{};
        if (o.cs_namespace.empty())
            o.cs_namespace = std::define_static_string(std::meta::identifier_of(Ns));
        doc.opts = std::move(o);
        // Root free functions / variables land in a `Global` static class; welded
        // types are flat in the namespace; each submodule gets its own static class.
        module_writer m{&doc, "Global"};
        ::welder::welder<rod, Style>::template weld_namespace<Ns>(m);
        shim << doc.render_shim();
        cs << doc.render_cs();
    }
};

static_assert(::welder::rod<rod>,
              "welder::rods::csharp::rod must satisfy welder::rod");

} // namespace welder::inline v0::rods::csharp
