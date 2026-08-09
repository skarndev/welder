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
#include <welder/rods/csharp/operators.hpp> // the C++-operator -> C# map + lookups
#include <welder/rods/csharp/directors.hpp> // director eligibility/slots (C# overrides)

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

/** The identifier of entity @a E as a runtime-usable static C string. */
template <std::meta::info E>
inline constexpr const char* ident_v =
    std::define_static_string(std::meta::identifier_of(E));

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
        case marshal_kind::optional_:   return "welder_opt_wire";
        case marshal_kind::seq_value:   return "welder_seq_wire";
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

/** A generated CONTAINER wrapper reference (`std::vector<welded>`): the
    rename key is the specialization's display string — `qualified_cpp_name`
    would collapse every instantiation to `::std::vector`. The final C# name
    ("Vector" + the element's name) is registered at collection
    (@ref rod::_ensure_vector) and itself contains the element placeholder,
    which the render pass's rescan resolves. */
template <std::meta::info C>
std::string container_ref() {
    static constexpr const char* d{
        std::define_static_string(std::meta::display_string_of(C))};
    return std::string{"\x01"} + d + "\x02";
}

/** A welded class/enum reference in a HANDLE-FIELD position: the
    identifier-safe placeholder flavor (a nested type's dotted name sanitizes
    to underscores at render). */
template <std::meta::info Bare>
std::string field_ref() {
    return std::string{"\x03"} + cpp_name_v<Bare> + "\x04";
}

/** The managed type the P/Invoke declaration uses for @a Type. @a is_return
    switches a string between its `in` (`string`) and `out` (`IntPtr`,
    caller-freed) forms; a welded-class parameter is typed as its `SafeHandle`
    subclass (premature-collection safety on the call), a return as `IntPtr`. */
/** A welded class/enum REFERENCE as a render-time placeholder: the final C#
    name is reconciled from the document's rename map at render() (filled by
    make_class/make_enum), so reference spelling never depends on declaration
    order or on a Style reaching the hook (add_operator has none). */
template <std::meta::info Bare>
std::string type_ref() {
    return std::string{"\x01"} + cpp_name_v<Bare> + "\x02";
}

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
        return type_ref<bare(Type)>();
    else if constexpr (k == marshal_kind::optional_)
        return "WelderOptWire";
    else if constexpr (k == marshal_kind::seq_value)
        return "WelderSeqWire";
    else if constexpr (k == marshal_kind::seq_ref)
        return is_return ? std::string{"IntPtr"}
                         : container_ref<bare(Type)>() + "Handle";
    else // handle
        return is_return ? std::string{"IntPtr"}
                         : type_ref<bare(Type)>() + "Handle";
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
    else if constexpr (k == marshal_kind::optional_) {
        constexpr marshal_kind pk{classify(optional_payload(bare(Type)))};
        if constexpr (pk == marshal_kind::scalar) {
            constexpr const char* c{
                scalar_spell(optional_payload(bare(Type))).cs};
            return std::string{c} + "?";
        } else if constexpr (pk == marshal_kind::boolean)
            return "bool?";
        else if constexpr (pk == marshal_kind::utf8_string)
            return "string?";
        else // enum_ / handle: nullable of the referenced type
            return type_ref<bare(optional_payload(bare(Type)))>() + "?";
    } else if constexpr (k == marshal_kind::seq_value) {
        if constexpr (classify(sequence_element(bare(Type))) ==
                      marshal_kind::enum_)
            return type_ref<bare(sequence_element(bare(Type)))>() + "[]";
        else {
            constexpr const char* c{
                scalar_spell(sequence_element(bare(Type))).cs};
            return std::string{c} + "[]";
        }
    } else if constexpr (k == marshal_kind::seq_ref)
        return container_ref<bare(Type)>();
    else
        return type_ref<bare(Type)>(); // enum_ / handle
}

/** The public C# RETURN type: like @ref public_type, plus the `?` nullable
    marker for a pointer-flavor welded-class return (a C++ `nullptr` maps to
    C# `null`). */
template <std::meta::info R, class Style>
std::string public_return_type() {
    if constexpr (classify(R) == marshal_kind::handle ||
                  classify(R) == marshal_kind::seq_ref) {
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
    else if constexpr (k == marshal_kind::handle ||
                       k == marshal_kind::seq_ref) {
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
    } else if constexpr (k == marshal_kind::optional_) {
        constexpr marshal_kind pk{classify(optional_payload(bare(R)))};
        std::string out{ind + "var __r = " + pc + ";\n" + check};
        if constexpr (pk == marshal_kind::utf8_string) {
            out += ind + "if (__r.Has == 0) return null;\n";
            out += ind + "IntPtr __s = __r.S;\n";
            out += ind + "try { return Marshal.PtrToStringUTF8(__s) ?? \"\"; }\n";
            out += ind + "finally { NativeMethods.welder_free(__s); }\n";
        } else if constexpr (pk == marshal_kind::handle) {
            out += ind + "return __r.Has != 0 ? new " +
                   type_ref<bare(optional_payload(bare(R)))>() +
                   "(__r.P, true) : null;\n";
        } else if constexpr (pk == marshal_kind::boolean) {
            out += ind + "return __r.Has != 0 ? (bool?)(__r.I != 0) : null;\n";
        } else if constexpr (pk == marshal_kind::enum_) {
            out += ind + "return __r.Has != 0 ? (" +
                   type_ref<bare(optional_payload(bare(R)))>() + "?)(" +
                   type_ref<bare(optional_payload(bare(R)))>() +
                   ")__r.I : null;\n";
        } else if constexpr (type_trait(^^std::is_floating_point_v,
                                        bare(optional_payload(bare(R))))) {
            constexpr const char* c{
                scalar_spell(optional_payload(bare(R))).cs};
            out += ind + "return __r.Has != 0 ? (" + std::string{c} +
                   "?)unchecked((" + c + ")__r.F) : null;\n";
        } else {
            constexpr const char* c{
                scalar_spell(optional_payload(bare(R))).cs};
            out += ind + "return __r.Has != 0 ? (" + std::string{c} +
                   "?)unchecked((" + c + ")__r.I) : null;\n";
        }
        return out;
    } else if constexpr (k == marshal_kind::seq_value) {
        std::string ecs{};
        if constexpr (classify(sequence_element(bare(R))) ==
                      marshal_kind::enum_)
            ecs = type_ref<bare(sequence_element(bare(R)))>();
        else {
            constexpr const char* c{scalar_spell(sequence_element(bare(R))).cs};
            ecs = c;
        }
        std::string out{ind + "var __r = " + pc + ";\n" + check};
        out += ind + "var __out = new " + ecs + "[__r.Len];\n";
        out += ind + "if (__r.Len != 0)\n" + ind + "{\n";
        out += ind + "    fixed (" + ecs + "* __d = __out)\n";
        out += ind + "        Buffer.MemoryCopy((void*)__r.Data, __d, __r.Len * "
               "sizeof(" + ecs + "), __r.Len * sizeof(" + ecs + "));\n";
        out += ind + "}\n";
        out += ind + "if (__r.Data != IntPtr.Zero) "
               "NativeMethods.welder_free(__r.Data);\n";
        out += ind + "return __out;\n";
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
    return ((classify(MT) == marshal_kind::handle ||
             classify(MT) == marshal_kind::seq_ref) &&
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
    std::string pin_open{};       /**< `fixed (...)` prefixes pinning array params. */
    bool needs_unsafe{false};     /**< pinning / raw copies ⇒ an `unsafe` wrapper. */
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
        // The param's STATIC type picks its own level's handle field — for a
        // derived instance passed as a base that is the correctly-upcast one.
        cp.wrapper_args += name + "._h_" + field_ref<bare(PT)>();
    else if constexpr (classify(PT) == marshal_kind::optional_) {
        constexpr marshal_kind pk{classify(optional_payload(bare(PT)))};
        if constexpr (pk == marshal_kind::utf8_string)
            cp.wrapper_args += name +
                               " is null ? default : new WelderOptWire { Has = "
                               "1, S = NativeMethods.welder_dup_utf8(" +
                               name + ") }";
        else if constexpr (pk == marshal_kind::handle)
            cp.wrapper_args += name +
                               " is null ? default : new WelderOptWire { Has = "
                               "1, P = " + name + "._h_" +
                               field_ref<bare(optional_payload(bare(PT)))>() +
                               ".DangerousGetHandle() }";
        else if constexpr (pk == marshal_kind::boolean)
            cp.wrapper_args += name + ".HasValue ? new WelderOptWire { Has = 1, "
                               "I = " + name + ".Value ? 1 : 0 } : default";
        else if constexpr (type_trait(^^std::is_floating_point_v,
                                      bare(optional_payload(bare(PT)))))
            cp.wrapper_args += name + ".HasValue ? new WelderOptWire { Has = 1, "
                               "F = (double)" + name + ".Value } : default";
        else // integral scalar / enum
            cp.wrapper_args += name + ".HasValue ? new WelderOptWire { Has = 1, "
                               "I = (long)" + name + ".Value } : default";
    } else if constexpr (classify(PT) == marshal_kind::seq_ref) {
        cp.wrapper_args += name + "._h_" + container_ref<bare(PT)>();
    } else if constexpr (classify(PT) == marshal_kind::seq_value) {
        const std::string pin{"__pin" + i};
        std::string ecs{};
        if constexpr (classify(sequence_element(bare(PT))) ==
                      marshal_kind::enum_)
            ecs = type_ref<bare(sequence_element(bare(PT)))>();
        else {
            constexpr const char* c{
                scalar_spell(sequence_element(bare(PT))).cs};
            ecs = c;
        }
        cp.pin_open += "fixed (" + ecs + "* " + pin + " = " + name + ") ";
        cp.needs_unsafe = true;
        cp.wrapper_args += "new WelderSeqWire { Data = (IntPtr)" + pin +
                           ", Len = " + name + ".Length }";
    } else
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
                   const std::string& self_cs = {},
                   const std::string& self_field = {}) {
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
    std::string call_args{HasSelf ? self_field : std::string{}};
    if (!cp.wrapper_args.empty())
        call_args += (call_args.empty() ? "" : ", ") + cp.wrapper_args;
    call_args += (call_args.empty() ? "" : ", ");
    call_args += "out WelderError __e";
    const std::string pc{"NativeMethods." + sym + "(" + call_args + ")"};
    emit_callable_docs<Fn>(wrapper_out, indent, cp);
    constexpr bool ret_unsafe{classify(std::meta::return_type_of(Fn)) ==
                              marshal_kind::seq_value};
    const bool is_unsafe{cp.needs_unsafe || ret_unsafe};
    wrapper_out += indent + "public " + (is_unsafe ? "unsafe " : "") +
                   (HasSelf ? "" : "static ") +
                   public_return_type<std::meta::return_type_of(Fn), Style>() +
                   " " + wrapper_name + "(" + cp.wrapper_params + ")\n" + indent +
                   "{\n";
    if (!cp.pin_open.empty())
        wrapper_out += indent + "    " + cp.pin_open + "{\n";
    wrapper_out += wrapper_return_body<std::meta::return_type_of(Fn), Style,
                                       ::welder::return_policy_of(Fn, lang::cs)>(
        pc, indent + "    ", HasSelf ? "this" : "");
    if (!cp.pin_open.empty())
        wrapper_out += indent + "    }\n";
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
    // The public constructor CHAINS through the internal (IntPtr, bool) one
    // (which initializes every base level's upcast handle), so construction
    // works identically for roots and derived classes. The static helper
    // exists because a chained `this(...)` argument cannot use `out var`.
    const std::string helper{"__New" + sym.substr(sym.rfind("_new") + 4)};
    w.members += "        private static " +
                 std::string{cp.needs_unsafe ? "unsafe " : ""} + "IntPtr " +
                 helper + "(" + cp.wrapper_params + ")\n        {\n" +
                 (cp.pin_open.empty() ? "" : "            " + cp.pin_open +
                                             "{\n") +
                 "            IntPtr __r = NativeMethods." + sym + "(" +
                 (cp.wrapper_args.empty() ? std::string{}
                                          : cp.wrapper_args + ", ") +
                 "out WelderError __e);\n"
                 "            WelderInterop.ThrowIfError(in __e);\n"
                 "            return __r;\n" +
                 (cp.pin_open.empty() ? "" : "            }\n") +
                 "        }\n";
    // Re-list the wrapper parameter NAMES for the chained call.
    std::string names{};
    for (const std::string& n : split_param_names(cp.param_names)) {
        names += (names.empty() ? "" : ", ");
        names += n;
    }
    w.members += "        public " + w.cs_name + "(" + cp.wrapper_params +
                 ") : this(" + helper + "(" + names + "), true) {" +
                 (w.is_director ? " __DirBind(); " : "") + "}\n\n";
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

    /** The CLS metadata name (`op_Addition`, …) for a mapped operator, or
        `nullptr` for the unmapped ones (assignment, compound assignment,
        `++`/`--`, dereference, …) — the driver's eligibility gate. */
    static consteval const char* special_method_name(std::meta::info op_fn) {
        return cs_operator(op_fn).cls_name;
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
        class_writer w{};
        w.doc = m.doc;
        w.cs_name = name;
        w.cs_path = name;
        w.doc_text = doc ? doc : "";
        w.cpp_qualified = cpp_name_v<Decl>;
        // References to T anywhere (params, returns, fields, operators) are
        // emitted as placeholders over the RAW spelling; register the final
        // name for the render-time reconciliation. The reference spelling is
        // ^^T's own (what type_ref emits), which for an alias-welded
        // specialization differs from Decl's — record both.
        m.doc->record_type_name(cpp_name_v<Decl>, name);
        if constexpr (std::meta::has_identifier(^^T))
            if (std::string{cpp_name_v<^^T>} != cpp_name_v<Decl>)
                m.doc->record_type_name(cpp_name_v<^^T>, name);
        w.sym_prefix = std::string{"welder_"} + upath_v<Decl>;
        w.handle_field = std::string{"_h_"} + name;
        w.handle_cs = std::string{name} + "Handle";
        w.destroy_symbol = w.sym_prefix + "_destroy";
        // Welded bases: the FIRST becomes the C# base class (the internal
        // constructor chains an upcast pointer down); every FURTHER one gets a
        // non-owning As<Base>() view — C# has single inheritance, so the extra
        // bases' surfaces are reached through their own wrapper. Each base
        // needs an upcast thunk: a compiled static_cast, so multiple/virtual
        // inheritance offsets are exact.
        std::size_t base_i{0};
        template for (constexpr auto base : std::define_static_array(Bases)) {
            const std::string bsym{w.sym_prefix + "_as_" +
                                   upath_v<std::meta::dealias(base)>};
            m.doc->record_symbol(bsym);
            m.doc->shim += "void* " + bsym + "(void* self) { return "
                           "wcs::shim::upcast<^^" + w.cpp_qualified + ", ^^" +
                           std::string{cpp_name_v<std::meta::dealias(base)>} +
                           ">(self); }\n\n";
            m.doc->pinvoke += "        [LibraryImport(Lib)] internal static "
                              "partial IntPtr " + bsym + "(IntPtr self);\n";
            const std::string bref{type_ref<std::meta::dealias(base)>()};
            if (base_i == 0) {
                w.base_ref = bref;
                w.base_upcast_sym = bsym;
            } else {
                w.members += "        /// <summary>This object's " + bref +
                             " base surface (a non-owning view).</summary>\n"
                             "        public " + bref + " As" + bref +
                             "()\n        {\n"
                             "            IntPtr __p = NativeMethods." + bsym +
                             "(" + w.handle_field + ".DangerousGetHandle());\n"
                             "            GC.KeepAlive(this);\n"
                             "            var __v = new " + bref +
                             "(__p, false);\n"
                             "            __v.__owner = this;\n"
                             "            return __v;\n        }\n\n";
            }
            ++base_i;
        }
        if constexpr (director_eligible(std::meta::dealias(^^T))) {
            w.is_director = true;
            w.director_ident = std::string{"__wcs_dir_"} + upath_v<Decl>;
            _emit_director<T>(m, w);
        }
        // The destructor thunk + its P/Invoke (the SafeHandle's release path).
        m.doc->record_symbol(w.destroy_symbol);
        m.doc->shim += "void " + w.destroy_symbol +
                       "(void* self) { wcs::shim::destroy<^^" + w.cpp_qualified +
                       ">(self); }\n\n";
        m.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial void " +
                          w.destroy_symbol + "(IntPtr self);\n";
        return w;
    }

    /** Place a NESTED member class under its enclosing type's binding: the
        writer flushes into the OUTER's members (a real C# nested type), its
        references resolve to the dotted path, and its handle field/class
        spellings derive from it (identifier-sanitized / dotted). */
    template <class T, auto Bases, std::size_t... I>
    static class_writer make_nested_class(module_type& m, class_writer& outer,
                                          const char* name, const char* doc,
                                          std::index_sequence<I...> seq) {
        return make_nested_class<T, ^^T, Bases>(m, outer, name, doc, seq);
    }

    /** The declaring-entity-aware nested form the carriage prefers. */
    template <class T, std::meta::info Decl, auto Bases, std::size_t... I>
    static class_writer make_nested_class(module_type& m, class_writer& outer,
                                          const char* name, const char* doc,
                                          std::index_sequence<I...> seq) {
        outer.nested_names.push_back(name);
        class_writer w{make_class<T, Decl, Bases>(m, name, doc, seq)};
        w.sink = &outer.members;
        w.cs_path = outer.cs_path + "." + name;
        w.handle_cs = w.cs_path + "Handle";
        std::string safe{w.cs_path};
        for (char& c : safe)
            if (c == '.')
                c = '_';
        w.handle_field = "_h_" + safe;
        // Refine the flat registration to the dotted path.
        m.doc->record_type_name(cpp_name_v<Decl>, w.cs_path);
        if constexpr (std::meta::has_identifier(^^T))
            if (std::string{cpp_name_v<^^T>} != cpp_name_v<Decl>)
                m.doc->record_type_name(cpp_name_v<^^T>, w.cs_path);
        return w;
    }

    /** Place a NESTED member enum under the outer's binding (same model). */
    template <class E>
    static enum_writer make_nested_enum(module_type& m, class_writer& outer,
                                        const char* name, const char* doc) {
        outer.nested_names.push_back(name);
        enum_writer w{make_enum<E>(m, name, doc)};
        w.sink = &outer.members;
        m.doc->record_type_name(cpp_name_v<std::meta::dealias(^^E)>,
                                outer.cs_path + "." + name);
        return w;
    }

    /** Nothing to finalize — the nested writer's RAII flush lands its block
        inside the outer's members before the outer itself flushes. */
    template <class T>
    static void finish_nested_class(module_type&, class_writer&, class_writer&,
                                    const char*) {}

    /** Emit the whole constructor surface in one call (main's contract): a
        no-argument form when @a HasDefault, one per member of @a Ctors (exact
        constructors, spliced via `ctor_at`), the aggregate field constructor
        when @a Aggregate, and — @a Copyable — the admitted copy constructor as
        the managed `Clone()` (C# has no copy-constructor protocol; a `T(other)`
        overload would collide with a one-argument user constructor). */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate, bool Copyable>
    static void add_constructors(class_writer& w) {
        const std::string anchor{"^^" + w.cpp_qualified};
        // A director-eligible type is C#-constructed AS its director subclass
        // (the handle stays "pointer to T" — construct_as adjusts), so a C#
        // subclass can override its virtuals; unoverridden slots fall through
        // to the qualified base call, so a plain C#-side instance behaves
        // identically to a T.
        const std::string dir_anchor{w.is_director ? "^^" + w.director_ident
                                                   : std::string{}};
        if constexpr (HasDefault) {
            emit_ctor(w, call_pieces{}, w.sym_prefix + "_new_default",
                      w.is_director
                          ? "wcs::shim::default_construct_as<" + dir_anchor +
                                ", " + anchor + ">"
                          : "wcs::shim::default_construct<" + anchor + ">");
        }
        template for (constexpr auto ctor : std::define_static_array(Ctors)) {
            constexpr std::size_t k{index_of_ctor(ctor)};
            constexpr std::size_t n{std::meta::parameters_of(ctor).size()};
            _collect_containers<ctor>(*w.doc);
            emit_ctor(w,
                      build_params<ctor, ::welder::naming::none>(
                          std::make_index_sequence<n>{}),
                      w.sym_prefix + "_new_" + std::to_string(k),
                      w.is_director
                          ? "wcs::shim::construct_as<" + dir_anchor + ", " +
                                anchor + ", wcs::ctor_at(" + anchor + ", " +
                                std::to_string(k) + ")>"
                          : "wcs::shim::construct<" + anchor + ", wcs::ctor_at(" +
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
                              "partial IntPtr " + sym + "(" + w.handle_cs +
                              " self, out WelderError err);\n";
            w.members += "        /// <summary>Copy this instance (the C++ copy "
                         "constructor).</summary>\n"
                         "        public " + w.cs_name + " Clone()\n"
                         "        {\n"
                         "            IntPtr __r = NativeMethods." + sym +
                         "(" + w.handle_field + ", out WelderError __e);\n"
                         "            WelderInterop.ThrowIfError(in __e);\n"
                         "            return new " + w.cs_name + "(__r, true);\n"
                         "        }\n\n";
        }
    }

    template <std::meta::info Mem, class Style = ::welder::naming::none>
    static void add_field(class_writer& w) {
        constexpr std::meta::info MT{std::meta::type_of(Mem)};
        if constexpr (classify(std::meta::type_of(Mem)) == marshal_kind::seq_ref)
            _ensure_vector<bare(std::meta::type_of(Mem))>(*w.doc);
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
                          " " + getsym + "(" + w.handle_cs +
                          " self, out WelderError err);\n";
        if constexpr (!read_only) {
            w.doc->record_symbol(setsym);
            w.doc->shim += "void " + setsym + "(void* self, " +
                           std::string{wire_param_v<std::meta::type_of(Mem)>} +
                           " v, welder_error* err) { return "
                           "wcs::shim::field_set<" + anchor + ", " + lookup +
                           ">(self, err, v); }\n\n";
            w.doc->pinvoke += "        " + import_attr(is_str) +
                              " internal static partial void " + setsym +
                              "(" + w.handle_cs + " self, " +
                              (is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
                              pinvoke_type<std::meta::type_of(Mem), Style>(false) +
                              " v, out WelderError err);\n";
        }

        // property (the setter's value conversion reuses the parameter
        // machinery — one conversion source for params, setters, operands)
        call_pieces vcp{};
        append_one_param<std::meta::type_of(Mem), Style>(vcp, 0, "value");
        const std::string pname{
            ::welder::name_of<Mem, lang::cs, Style, ::welder::ent_kind::field>()};
        w.surface_names.push_back(pname);
        emit_doc_comment(w.members, "        ", ::welder::doc_of<Mem>());
        constexpr bool unsafe_prop{classify(std::meta::type_of(Mem)) ==
                                   marshal_kind::seq_value};
        w.members += "        public " +
                     std::string{unsafe_prop ? "unsafe " : ""} +
                     public_type<std::meta::type_of(Mem), Style>() + " " + pname +
                     "\n        {\n            get\n            {\n";
        w.members += wrapper_return_body<std::meta::type_of(Mem), Style,
                                         field_return_policy(
                                             std::meta::type_of(Mem))>(
            "NativeMethods." + getsym + "(" + w.handle_field +
                ", out WelderError __e)",
            "                ", "this");
        w.members += "            }\n";
        if constexpr (!read_only) {
            w.members += "            set\n            {\n" +
                         (vcp.pin_open.empty()
                              ? std::string{}
                              : "                " + vcp.pin_open + "{\n") +
                         "                NativeMethods." + setsym + "(" +
                         w.handle_field + ", " + vcp.wrapper_args +
                         ", out WelderError __e);\n"
                         "                WelderInterop.ThrowIfError(in __e);\n" +
                         (vcp.pin_open.empty() ? std::string{}
                                               : "                }\n") +
                         "            }\n";
        }
        w.members += "        }\n\n";
    }

    /** One resolved method-backed property: the getter's thunk under a
        property-read symbol, the (optional) setter's under a write symbol, and
        a C# property calling them under the driver-resolved @a name. */
    template <class T, std::meta::info Getter, std::meta::info Setter>
    static void add_property(class_writer& w, const char* name) {
        w.surface_names.push_back(name);
        _collect_containers<Getter>(*w.doc);
        if constexpr (Setter != std::meta::info{})
            _collect_containers<Setter>(*w.doc);
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
                          " " + getsym + "(" + w.handle_cs +
                          " self, out WelderError err);\n";

        emit_doc_comment(w.members, "        ", ::welder::doc_of<Getter>());
        w.members += "        public " +
                     public_type<std::meta::remove_cvref(std::meta::return_type_of(Getter)), ::welder::naming::none>() +
                     " " + name + "\n        {\n            get\n            {\n";
        w.members += wrapper_return_body<std::meta::return_type_of(Getter),
                                         ::welder::naming::none,
                                         ::welder::return_policy_of(
                                             Getter, lang::cs)>(
            "NativeMethods." + getsym + "(" + w.handle_field +
                ", out WelderError __e)",
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
            w.doc->pinvoke += "        " + import_attr(p_is_str) +
                              " internal static partial void " + setsym +
                              "(" + w.handle_cs + " self, " +
                              (p_is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
                              pinvoke_type<first_param_type(Setter),
                                           ::welder::naming::none>(false) +
                              " v, out WelderError err);\n";
            call_pieces vcp{};
            append_one_param<first_param_type(Setter), ::welder::naming::none>(
                vcp, 0, "value");
            w.members += "            set\n            {\n" +
                         (vcp.pin_open.empty()
                              ? std::string{}
                              : "                " + vcp.pin_open + "{\n") +
                         "                NativeMethods." + setsym + "(" +
                         w.handle_field + ", " + vcp.wrapper_args +
                         ", out WelderError __e);\n"
                         "                WelderInterop.ThrowIfError(in __e);\n" +
                         (vcp.pin_open.empty() ? std::string{}
                                               : "                }\n") +
                         "            }\n";
        }
        w.members += "        }\n\n";
    }

    /** Emit method overload group @a Fns as natural C# overloads sharing one
        name, each with its own indexed symbol (the group's name resolves from
        `Fns[0]`; the index re-derives the exact overload shim-side). An
        overridable virtual slot of a director-eligible type emits as a
        `public virtual` method that dispatches by origin — the ordinary
        virtual thunk for a C++-originated object, the qualified base-call
        thunk for a director (C# has already resolved the dynamic dispatch;
        this is also what terminates `base.Method()` inside an override). */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_method(class_writer& w) {
        const std::string name{::welder::name_of<Fns[0], lang::cs, Style,
                                                 ::welder::ent_kind::method>()};
        w.surface_names.push_back(name);
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
            static constexpr const char* fsig{std::define_static_string(
                std::meta::display_string_of(std::meta::type_of(fn)))};
            // The director scaffolding names slots through render-time
            // placeholders keyed by (declaring class, identifier, signature) —
            // record every bound method, so an inherited slot resolves through
            // the BASE wrapper's binding.
            w.doc->record_type_name(
                std::string{cpp_name_v<std::meta::parent_of(fn)>} + "#" + id +
                    "#" + fsig,
                name);
            _collect_containers<fn>(*w.doc);
            std::size_t vslot_k{static_cast<std::size_t>(-1)};
            if constexpr (std::meta::is_virtual(fn)) {
                if (w.is_director) {
                    static constexpr const char* vid{std::define_static_string(
                        std::meta::identifier_of(fn))};
                    for (const auto& vs : w.vslots)
                        if (std::string_view{vs.name} == vid &&
                            std::string_view{vs.sig} == fsig)
                            vslot_k = vs.k;
                }
            }
            if (vslot_k != static_cast<std::size_t>(-1)) {
                _emit_virtual_method<fn, Style>(w, name, sym, expr, vslot_k);
            } else {
                emit_callable<fn, Style, true>(*w.doc, sym, w.members,
                                               "        ", name, expr,
                                               w.handle_cs,
                                               w.handle_field);
            }
        }
    }

  protected:
    /** Emit virtual slot @a Fn: the ordinary (virtual-dispatch) thunk, a
        qualified base-call thunk, and a `public virtual` wrapper branching on
        `__isDirector`. Records the slot's C# name for the director
        scaffolding's placeholders. */
    template <std::meta::info Fn, class Style>
    static void _emit_virtual_method(class_writer& w, const std::string& name,
                                     const std::string& sym,
                                     const std::string& expr,
                                     std::size_t slot) {
        {
            const std::string ks{std::to_string(slot)};

            ::welder::validate_return_policy<Fn, lang::cs>();
            constexpr bool ret_checked{
                (require_marshallable(std::meta::return_type_of(Fn), true),
                 true)};
            static_assert(ret_checked);
            constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
            const call_pieces cp{
                build_params<Fn, Style>(std::make_index_sequence<n>{})};
            const std::string bsym{sym + "_base"};
            w.doc->record_symbol(sym);
            w.doc->record_symbol(bsym);

            const std::string idx{"wcs::director_slot(^^" + w.cpp_qualified +
                                  ", " + ks + ")"};
            std::string shim_params{"void* self" +
                                    (cp.shim_params.empty()
                                         ? std::string{}
                                         : ", " + cp.shim_params) +
                                    ", welder_error* err"};
            std::string delegate_args{"self, err" +
                                      (cp.delegate_args.empty()
                                           ? std::string{}
                                           : ", " + cp.delegate_args)};
            // 1) the ordinary thunk (virtual dispatch through the pmf)
            w.doc->shim += std::string{
                               wire_return_v<std::meta::return_type_of(Fn)>} +
                           " " + sym + "(" +
                           shim_params + ") { return " + expr + "(" +
                           delegate_args + "); }\n\n";
            // 2) the qualified base-call thunk (never re-enters the director)
            {
                std::string conv{};
                [[maybe_unused]] std::size_t j{0};
                template for ([[maybe_unused]] constexpr auto p :
                              std::define_static_array(
                                  std::meta::parameters_of(Fn))) {
                    const std::string js{std::to_string(j)};
                    conv += (j ? ", " : "");
                    conv += "wcs::shim::to_cpp<::std::meta::type_of(::std::"
                            "meta::parameters_of(" +
                            idx + ")[" + js + "])>(a" + js + ")";
                    ++j;
                }
                static constexpr const char* fid{std::define_static_string(
                    std::meta::identifier_of(Fn))};
                w.doc->shim +=
                    std::string{
                        wire_return_v<std::meta::return_type_of(Fn)>} +
                    " " + bsym + "(" +
                    shim_params + ") {\n    auto* __o = reinterpret_cast<" +
                    w.cpp_qualified +
                    "*>(self);\n    return wcs::shim::guarded<"
                    "::std::meta::return_type_of(" +
                    idx + ")>(err, [&]() -> decltype(auto) { return __o->" +
                    w.cpp_qualified + "::" + fid + "(" + conv +
                    "); });\n}\n\n";
            }
            // P/Invokes for both
            std::string pin_params{w.handle_cs + " self" +
                                   (cp.pinvoke_params.empty()
                                        ? std::string{}
                                        : ", " + cp.pinvoke_params) +
                                   ", out WelderError err"};
            constexpr bool r_is_bool{
                classify(std::meta::return_type_of(Fn)) ==
                marshal_kind::boolean};
            const std::string ret_attr{
                r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : ""};
            for (const std::string& s2 : {sym, bsym})
                w.doc->pinvoke += "        " + import_attr(cp.has_string) + " " +
                                  ret_attr + "internal static partial " +
                                  pinvoke_type<std::meta::return_type_of(Fn),
                                               Style>(true) +
                                  " " + s2 + "(" + pin_params + ");\n";
            // 3) the public virtual wrapper, branching by origin
            emit_callable_docs<Fn>(w.members, "        ", cp);
            std::string call_args{w.handle_field +
                                  (cp.wrapper_args.empty()
                                       ? std::string{}
                                       : ", " + cp.wrapper_args) +
                                  ", out WelderError __e"};
            w.members += "        public virtual " +
                         public_return_type<std::meta::return_type_of(Fn),
                                            Style>() +
                         " " + name + "(" + cp.wrapper_params +
                         ")\n        {\n            if (__isDirector)\n"
                         "            {\n";
            w.members += wrapper_return_body<std::meta::return_type_of(Fn),
                                             Style,
                                             ::welder::return_policy_of(
                                                 Fn, lang::cs)>(
                "NativeMethods." + bsym + "(" + call_args + ")",
                "                ", "this");
            w.members += "            }\n            else\n            {\n";
            w.members += wrapper_return_body<std::meta::return_type_of(Fn),
                                             Style,
                                             ::welder::return_policy_of(
                                                 Fn, lang::cs)>(
                "NativeMethods." + sym + "(" + call_args + ")",
                "                ", "this");
            w.members += "            }\n        }\n\n";
        }
    }

  public:

    /** Emit static-method overload group @a Fns as `public static` overloads. */
    template <auto Fns, class Style = ::welder::naming::none>
    static void add_static_method(class_writer& w) {
        const std::string name{
            ::welder::name_of<Fns[0], lang::cs, Style,
                              ::welder::ent_kind::static_method>()};
        w.surface_names.push_back(name);
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            constexpr std::size_t k{index_of_named_member(fn)};
            _collect_containers<fn>(*w.doc);
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

    /** Emit operator slot group @a Fns — member and anchored free entries
        mixed, each with its own indexed symbol. Arithmetic/bitwise/unary map
        to `public static` C# operators (a reflected free entry — @a T on the
        right — emits with its declared operand order, which C# permits as
        long as one operand is the class); comparisons route through the class
        writer's pairing ledger (see `document.hpp`); `operator[]` becomes a
        get-only indexer; `operator()` an `Invoke` method. */
    template <class T, auto Fns>
    static void add_operator(class_writer& w) {
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            _emit_operator<T, fn>(w);
        }
    }

    /** Synthesize the C# relational operators from `operator<=>` group @a Fns:
        one wire thunk per overload collapses the ordering to an int
        (`shim::compare` evaluates `l <=> r` through C++'s own rewriting rules,
        heterogeneous operands included), and the four relational operators
        derive from it — minus the slots an explicit participating operator
        already @a Covered. A heterogeneous overload also emits the reversed
        operand order (C++'s rewritten `5 < obj` works, so C#'s should). `==`
        is never synthesized (mirroring C++: only `operator==` rewrites it —
        a defaulted spaceship's implicit `==` arrives via the operator path). */
    template <class T, auto Fns, auto Covered>
    static void add_comparisons(class_writer& w) {
        template for (constexpr auto fn : std::define_static_array(Fns)) {
            _emit_comparison_set<T, fn, Covered>(w);
        }
    }

    /** Bind the swept free ostream inserter @a Fn as `ToString()` (via
        @ref welder::detail::stringify, dup'd across the wire). */
    template <class T, std::meta::info Fn>
    static void add_stringifier(class_writer& w) {
        const std::string sym{w.sym_prefix + "_str"};
        w.doc->record_symbol(sym);
        constexpr std::size_t k{index_of_operator(Fn)};
        static constexpr const char* opid{std::define_static_string(
            operator_enum_ident(std::meta::operator_of(Fn)))};
        w.doc->shim += "const char* " + sym +
                       "(void* self, welder_error* err) { return "
                       "wcs::shim::stringify_text<^^" + w.cpp_qualified +
                       ", wcs::named_operator(^^" +
                       std::string{cpp_name_v<std::meta::parent_of(Fn)>} +
                       ", std::meta::operators::" + opid + ", false, " +
                       std::to_string(k) + ")>(self, err); }\n\n";
        w.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial "
                          "IntPtr " + sym + "(" + w.handle_cs +
                          " self, out WelderError err);\n";
        w.members += "        public override string ToString()\n        {\n";
        w.members += wrapper_return_body<^^std::string, ::welder::naming::none>(
            "NativeMethods." + sym + "(" + w.handle_field +
                ", out WelderError __e)",
            "            ");
        w.members += "        }\n\n";
    }

  protected:
    /** The `wcs::named_operator(...)` lookup text for operator @a Fn. */
    template <std::meta::info Fn>
    static std::string _operator_lookup() {
        static constexpr const char* opid{std::define_static_string(
            operator_enum_ident(std::meta::operator_of(Fn)))};
        constexpr bool u{::welder::detail::is_unary_operator(Fn)};
        constexpr std::size_t k{index_of_operator(Fn)};
        return std::string{"wcs::named_operator(^^"} +
               cpp_name_v<std::meta::parent_of(Fn)> +
               ", std::meta::operators::" + opid + (u ? ", true, " : ", false, ") +
               std::to_string(k) + ")";
    }

    /** The `_op_<token>_<u|b>_<k>` symbol suffix for operator @a Fn. */
    template <std::meta::info Fn>
    static std::string _operator_sym(const class_writer& w) {
        static constexpr const char* opid{std::define_static_string(
            operator_enum_ident(std::meta::operator_of(Fn)))};
        constexpr bool u{::welder::detail::is_unary_operator(Fn)};
        constexpr std::size_t k{index_of_operator(Fn)};
        // strip the "op_" prefix for the symbol leaf
        return w.sym_prefix + "_op_" + (opid + 3) + (u ? "_u_" : "_b_") +
               std::to_string(k);
    }

    /** Emit one operator overload @a Fn of welded type @a T. */
    template <class T, std::meta::info Fn>
    static void _emit_operator(class_writer& w) {
        constexpr cs_op_info oi{cs_operator(Fn)};
        static_assert(oi.kind != cs_op_kind::none,
                      "welder: unmapped operator reached add_operator");
        ::welder::validate_return_policy<Fn, lang::cs>();
        constexpr bool is_member{std::meta::is_class_member(Fn)};
        constexpr std::size_t n{std::meta::parameters_of(Fn).size()};
        const call_pieces cp{
            build_params<Fn, ::welder::naming::none>(std::make_index_sequence<n>{})};
        const std::string sym{_operator_sym<Fn>(w)};
        w.doc->record_symbol(sym);

        // --- shim thunk + P/Invoke (uniform: member -> method, free -> function)
        std::string shim_params{cp.shim_params};
        std::string delegate_args{};
        std::string pin_params{cp.pinvoke_params};
        if constexpr (is_member) {
            shim_params = "void* self" +
                          (cp.shim_params.empty() ? "" : ", " + cp.shim_params);
            delegate_args = "self, err";
            pin_params = w.handle_cs + " self" +
                         (cp.pinvoke_params.empty() ? "" : ", " + cp.pinvoke_params);
        } else {
            delegate_args = "err";
        }
        if (!cp.delegate_args.empty())
            delegate_args += ", " + cp.delegate_args;
        shim_params += (shim_params.empty() ? "" : ", ");
        shim_params += "welder_error* err";
        pin_params += (pin_params.empty() ? "" : ", ");
        pin_params += "out WelderError err";
        const std::string expr{
            is_member ? "wcs::shim::method<^^" + w.cpp_qualified + ", " +
                            _operator_lookup<Fn>() + ">"
                      : "wcs::shim::function<" + _operator_lookup<Fn>() + ">"};
        w.doc->shim += std::string{
                           wire_return_v<std::meta::return_type_of(Fn)>} +
                       " " + sym + "(" + shim_params + ") { return " + expr +
                       "(" + delegate_args + "); }\n\n";
        constexpr bool r_is_bool{classify(std::meta::return_type_of(Fn)) ==
                                 marshal_kind::boolean};
        w.doc->pinvoke +=
            "        " + import_attr(cp.has_string) + " " +
            (r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "") +
            "internal static partial " +
            pinvoke_type<std::meta::return_type_of(Fn),
                         ::welder::naming::none>(true) +
            " " + sym + "(" + pin_params + ");\n";

        // --- managed surface, by shape --------------------------------------
        // Operand spellings: `this`-typed for a member's left, declared
        // otherwise; the wrapper call rebuilds the argument expressions with
        // the operator parameter names (member left = `l`, others per shape).
        if constexpr (oi.kind == cs_op_kind::invoke) {
            // operator() -> a plain Invoke method (self + params).
            std::string args{w.handle_field +
                             (cp.wrapper_args.empty() ? "" : ", " + cp.wrapper_args)};
            w.members += "        public " +
                         public_return_type<std::meta::return_type_of(Fn),
                                            ::welder::naming::none>() +
                         " Invoke(" + cp.wrapper_params + ")\n        {\n";
            w.members += wrapper_return_body<std::meta::return_type_of(Fn),
                                             ::welder::naming::none,
                                             ::welder::return_policy_of(
                                                 Fn, lang::cs)>(
                "NativeMethods." + sym + "(" + args + ", out WelderError __e)",
                "            ", "this");
            w.members += "        }\n\n";
        } else if constexpr (oi.kind == cs_op_kind::indexer) {
            if (std::find(w.indexer_sigs.begin(), w.indexer_sigs.end(),
                          cp.wrapper_params) != w.indexer_sigs.end())
                return; // const/non-const C++ pair -> one C# indexer
            w.indexer_sigs.push_back(cp.wrapper_params);
            w.members += "        public " +
                         public_return_type<std::meta::return_type_of(Fn),
                                            ::welder::naming::none>() +
                         " this[" + cp.wrapper_params +
                         "]\n        {\n            get\n            {\n";
            w.members += wrapper_return_body<std::meta::return_type_of(Fn),
                                             ::welder::naming::none,
                                             ::welder::return_policy_of(
                                                 Fn, lang::cs)>(
                "NativeMethods." + sym + "(" + w.handle_field + ", " +
                    cp.wrapper_args + ", out WelderError __e)",
                "                ", "this");
            w.members += "            }\n        }\n\n";
        } else {
            // A static operator. Operand list: member -> (T l, P r) / (T v);
            // free -> the declared order (reflected entries included — C#
            // allows any order as long as one operand is the class).
            std::vector<std::string> op_types{};
            std::vector<std::string> op_names{};
            std::string args{}; // the P/Invoke argument expressions
            if constexpr (is_member) {
                op_types.push_back(type_ref<^^T>());
                op_names.push_back("l");
                args = "l._h_" + field_ref<^^T>();
            }
            // Guard the empty pack: param_types<Fn> must not instantiate for
            // a parameterless callable (a unary member operator) — the
            // array<info, 0> gotcha (see build_params).
            if constexpr (n != 0) {
                static constexpr auto pts{::welder::detail::param_types<Fn>()};
                std::size_t j{0};
                template for (constexpr auto pt : std::define_static_array(pts)) {
                    const std::string nm{is_member ? "r" : (j == 0 ? "l" : "r")};
                    op_types.push_back(public_type<pt, ::welder::naming::none>());
                    op_names.push_back(nm);
                    args += (args.empty() ? "" : ", ");
                    if constexpr (classify(pt) == marshal_kind::handle)
                        args += nm + "._h_" + field_ref<bare(pt)>();
                    else
                        args += nm;
                    ++j;
                }
            }
            std::string params{};
            for (std::size_t i{0}; i < op_types.size(); ++i) {
                params += (params.empty() ? "" : ", ");
                params += op_types[i] + " " + op_names[i];
            }
            std::string body{wrapper_return_body<
                std::meta::return_type_of(Fn), ::welder::naming::none,
                ::welder::return_policy_of(Fn, lang::cs)>(
                "NativeMethods." + sym + "(" + args + ", out WelderError __e)",
                "            ")};
            if constexpr (oi.kind == cs_op_kind::comparison) {
                // Binary always; the pairing ledger decides operator-vs-named
                // at flush.
                class_writer::cs_comparison c{};
                c.op = oi.symbol;
                c.lhs = op_types[0];
                c.rhs = op_types[1];
                c.ret = public_return_type<std::meta::return_type_of(Fn),
                                           ::welder::naming::none>();
                c.body = std::move(body);
                if (!w._have_comparison(c.op, c.lhs, c.rhs))
                    w.comparisons.push_back(std::move(c));
            } else if constexpr (oi.kind == cs_op_kind::unary) {
                w.members += "        public static " +
                             public_return_type<std::meta::return_type_of(Fn),
                                                ::welder::naming::none>() +
                             " operator " + oi.symbol + "(" + op_types[0] +
                             " v)\n        {\n";
                // The single operand is named `l`/`v` per shape; rebuild the
                // call with `v`.
                w.members += wrapper_return_body<
                    std::meta::return_type_of(Fn), ::welder::naming::none,
                    ::welder::return_policy_of(Fn, lang::cs)>(
                    "NativeMethods." + sym + "(" +
                        (is_member ? "v._h_" + field_ref<^^T>() : std::string{"v"}) +
                        ", out WelderError __e)",
                    "            ");
                w.members += "        }\n\n";
            } else {
                w.members += "        public static " +
                             public_return_type<std::meta::return_type_of(Fn),
                                                ::welder::naming::none>() +
                             " operator " + oi.symbol + "(" + params +
                             ")\n        {\n" + body + "        }\n\n";
            }
        }
    }

    /** Emit the compare thunk + the (un-@a Covered) relational operators for
        one spaceship overload @a Fn of @a T. */
    template <class T, std::meta::info Fn, auto Covered>
    static void _emit_comparison_set(class_writer& w) {
        constexpr std::size_t k{index_of_operator(Fn)};
        const std::string sym{w.sym_prefix + "_cmp_" + std::to_string(k)};
        w.doc->record_symbol(sym);
        const std::string lookup{_operator_lookup<Fn>()};
        // The operand: spelled through the SAME consteval re-derivation the
        // generator used, so the shim's compare<> sees the identical type.
        const std::string opnd{"::welder::detail::comparison_operand(" + lookup +
                               ", ^^" + w.cpp_qualified + ")"};
        w.doc->shim +=
            "std::int32_t " + sym + "(void* self, " +
            std::string{wire_param_v<::welder::detail::comparison_operand(
                Fn, ^^T)>} +
            " a0, welder_error* err) { return wcs::shim::compare<^^" +
            w.cpp_qualified + ", " + opnd + ">(self, err, a0); }\n\n";
        constexpr bool p_is_bool{
            classify(::welder::detail::comparison_operand(Fn, ^^T)) ==
            marshal_kind::boolean};
        w.doc->pinvoke +=
            "        [LibraryImport(Lib)] internal static partial int " + sym +
            "(" + w.handle_cs + " self, " +
            (p_is_bool ? "[MarshalAs(UnmanagedType.U1)] " : "") +
            pinvoke_type<::welder::detail::comparison_operand(Fn, ^^T),
                         ::welder::naming::none>(false) +
            " a0, out WelderError err);\n";

        const std::string lhs{type_ref<^^T>()};
        const std::string rhs{public_type<::welder::detail::comparison_operand(
                                              Fn, ^^T),
                                          ::welder::naming::none>()};
        constexpr bool rhs_is_handle{
            classify(::welder::detail::comparison_operand(Fn, ^^T)) ==
            marshal_kind::handle};
        // Computed under if constexpr: field_ref must not instantiate for a
        // scalar operand (no parent to spell).
        std::string rhs_field{};
        if constexpr (rhs_is_handle)
            rhs_field = "._h_" + field_ref<bare(::welder::detail::
                                                    comparison_operand(
                                                        Fn, ^^T))>();
        const bool hetero{lhs != rhs};
        auto record = [&](const char* op, bool reversed, const char* cond) {
            class_writer::cs_comparison c{};
            c.op = op;
            c.lhs = reversed ? rhs : lhs;
            c.rhs = reversed ? lhs : rhs;
            c.ret = "bool";
            const std::string self_arg{(reversed ? "r._h_" : "l._h_") +
                                       field_ref<^^T>()};
            std::string other{reversed ? "l" : "r"};
            other += rhs_field;
            c.body = "            var __c = NativeMethods." + sym + "(" +
                     self_arg + ", " + other +
                     ", out WelderError __e);\n"
                     "            WelderInterop.ThrowIfError(in __e);\n"
                     "            return " + cond + ";\n";
            if (!w._have_comparison(c.op, c.lhs, c.rhs))
                w.comparisons.push_back(std::move(c));
        };
        if constexpr (!Covered[0]) record("<", false, "__c == -1");
        if constexpr (!Covered[1]) record("<=", false, "__c == -1 || __c == 0");
        if constexpr (!Covered[2]) record(">", false, "__c == 1");
        if constexpr (!Covered[3]) record(">=", false, "__c == 0 || __c == 1");
        if (hetero) {
            // The reversed operand order (C++'s rewritten `5 < obj`): the
            // relation flips around the same thunk.
            if constexpr (!Covered[2]) record("<", true, "__c == 1");
            if constexpr (!Covered[3]) record("<=", true, "__c == 0 || __c == 1");
            if constexpr (!Covered[0]) record(">", true, "__c == -1");
            if constexpr (!Covered[1]) record(">=", true, "__c == -1 || __c == 0");
        }
    }

    /** Generate the reference-semantic wrapper for `std::vector<welded>`
        container type @a C (once per distinct instantiation): the native op
        thunks (delegating into `shim::vec_*`), their P/Invokes, the rename
        registration and the C# wrapper class (live element views pinned to
        the vector wrapper — welder's opaque-container model). */
    template <std::meta::info C>
    static void _ensure_vector(document& doc) {
        static constexpr const char* key{
            std::define_static_string(std::meta::display_string_of(C))};
        if (!doc.claim_container(key))
            return;
        const std::string sym{std::string{"welder_vec_"} +
                              upath_v<bare(sequence_element(C))>};
        const std::string eq{"^^" +
                             std::string{cpp_name_v<bare(sequence_element(C))>}};
        const std::string V{container_ref<C>()};
        const std::string E{type_ref<bare(sequence_element(C))>()};
        const std::string Ef{field_ref<bare(sequence_element(C))>()};
        // The wrapper's NAME must be an identifier, so it derives from the
        // element's identifier-safe form (a nested element's dots sanitize).
        doc.record_type_name(key, "Vector" + Ef);
        for (const char* leaf : {"_new", "_destroy", "_size", "_get", "_set",
                                 "_add", "_clear"})
            doc.record_symbol(sym + leaf);
        doc.shim +=
            "void* " + sym + "_new(welder_error* err) { return "
            "wcs::shim::vec_new<" + eq + ">(err); }\n\n"
            "void " + sym + "_destroy(void* self) { wcs::shim::vec_destroy<" +
            eq + ">(self); }\n\n"
            "std::int64_t " + sym + "_size(void* self, welder_error* err) { "
            "return wcs::shim::vec_size<" + eq + ">(self, err); }\n\n"
            "void* " + sym + "_get(void* self, std::int64_t i, welder_error* "
            "err) { return wcs::shim::vec_get<" + eq + ">(self, i, err); }\n\n"
            "void " + sym + "_set(void* self, std::int64_t i, void* elem, "
            "welder_error* err) { wcs::shim::vec_set<" + eq +
            ">(self, i, elem, err); }\n\n"
            "void " + sym + "_add(void* self, void* elem, welder_error* err) { "
            "wcs::shim::vec_add<" + eq + ">(self, elem, err); }\n\n"
            "void " + sym + "_clear(void* self, welder_error* err) { "
            "wcs::shim::vec_clear<" + eq + ">(self, err); }\n\n";
        doc.pinvoke +=
            "        [LibraryImport(Lib)] internal static partial IntPtr " +
            sym + "_new(out WelderError err);\n"
            "        [LibraryImport(Lib)] internal static partial void " + sym +
            "_destroy(IntPtr self);\n"
            "        [LibraryImport(Lib)] internal static partial long " + sym +
            "_size(" + V + "Handle self, out WelderError err);\n"
            "        [LibraryImport(Lib)] internal static partial IntPtr " +
            sym + "_get(" + V + "Handle self, long i, out WelderError err);\n"
            "        [LibraryImport(Lib)] internal static partial void " + sym +
            "_set(" + V + "Handle self, long i, " + E +
            "Handle elem, out WelderError err);\n"
            "        [LibraryImport(Lib)] internal static partial void " + sym +
            "_add(" + V + "Handle self, " + E +
            "Handle elem, out WelderError err);\n"
            "        [LibraryImport(Lib)] internal static partial void " + sym +
            "_clear(" + V + "Handle self, out WelderError err);\n";
        doc.containers +=
            "    internal sealed class " + V + "Handle : SafeHandle\n    {\n"
            "        internal " + V + "Handle(IntPtr handle, bool owns) : "
            "base(IntPtr.Zero, owns)\n        {\n            "
            "SetHandle(handle);\n        }\n"
            "        public override bool IsInvalid => handle == "
            "IntPtr.Zero;\n"
            "        protected override bool ReleaseHandle()\n        {\n"
            "            NativeMethods." + sym + "_destroy(handle);\n"
            "            return true;\n        }\n    }\n\n"
            "    /// <summary>A reference-semantic C++ vector of " + E +
            " (live element views).</summary>\n"
            "    public sealed class " + V + " : IDisposable\n    {\n"
            "        internal " + V + "Handle _h_" + V + ";\n"
            "        internal object? __owner;\n"
            "        internal " + V + "(IntPtr handle, bool owns) { _h_" + V +
            " = new " + V + "Handle(handle, owns); }\n"
            "        public " + V + "() : this(__New(), true) {}\n"
            "        private static IntPtr __New()\n        {\n"
            "            IntPtr __r = NativeMethods." + sym +
            "_new(out WelderError __e);\n"
            "            WelderInterop.ThrowIfError(in __e);\n"
            "            return __r;\n        }\n"
            "        public int Count\n        {\n            get\n"
            "            {\n"
            "                var __r = NativeMethods." + sym + "_size(_h_" + V +
            ", out WelderError __e);\n"
            "                WelderInterop.ThrowIfError(in __e);\n"
            "                return (int)__r;\n            }\n        }\n"
            "        public " + E + " this[int i]\n        {\n"
            "            get\n            {\n"
            "                IntPtr __r = NativeMethods." + sym + "_get(_h_" +
            V + ", i, out WelderError __e);\n"
            "                WelderInterop.ThrowIfError(in __e);\n"
            "                var __v = new " + E + "(__r, false);\n"
            "                __v.__owner = this;\n"
            "                return __v;\n            }\n"
            "            set\n            {\n"
            "                NativeMethods." + sym + "_set(_h_" + V + ", i, "
            "value._h_" + Ef + ", out WelderError __e);\n"
            "                WelderInterop.ThrowIfError(in __e);\n"
            "            }\n        }\n"
            "        public void Add(" + E + " item)\n        {\n"
            "            NativeMethods." + sym + "_add(_h_" + V + ", item._h_" +
            Ef + ", out WelderError __e);\n"
            "            WelderInterop.ThrowIfError(in __e);\n        }\n"
            "        public void Clear()\n        {\n"
            "            NativeMethods." + sym + "_clear(_h_" + V +
            ", out WelderError __e);\n"
            "            WelderInterop.ThrowIfError(in __e);\n        }\n"
            "        public void Dispose() => _h_" + V + ".Dispose();\n"
            "    }\n\n";
    }

    /** Collect the generated-wrapper containers a callable's signature uses. */
    template <std::meta::info Fn>
    static void _collect_containers(document& doc) {
        if constexpr (!std::meta::is_constructor(Fn)) {
            if constexpr (classify(std::meta::return_type_of(Fn)) ==
                          marshal_kind::seq_ref)
                _ensure_vector<bare(std::meta::return_type_of(Fn))>(doc);
        }
        template for ([[maybe_unused]] constexpr auto p :
                      std::define_static_array(std::meta::parameters_of(Fn))) {
            if constexpr (classify(std::meta::type_of(p)) ==
                          marshal_kind::seq_ref)
                _ensure_vector<bare(std::meta::type_of(p))>(doc);
        }
    }

    /** Emit the whole director apparatus for welded virtual type @a T: the
        C++ director subclass (into the document's `directors` section), its
        registration/bind thunks, and the managed scaffolding (callbacks, the
        override mask, `__DirBind`). See `directors.hpp` for the model. */
    template <class T>
    static void _emit_director(module_type& m, class_writer& w) {
        static constexpr auto slots{
            std::define_static_array(director_slots(std::meta::dealias(^^T)))};
        const std::string qual{w.cpp_qualified};
        const std::string dir{w.director_ident};
        const std::string idx_base{"wcs::director_slot(^^" + qual + ", "};

        // --- the C++ director subclass -----------------------------------
        std::string d{};
        // Plain (uninitialized) members + a value-initialized static: an NSDMI
        // in the nested table would be required before the enclosing class is
        // complete (the static member's {} sits in-class).
        std::string tbl{"    struct __wcs_table_t {\n"
                        "        void (*release)(void*);\n"};
        d += "struct " + dir + " final : " + qual + " {\n";
        d += "    using " + qual + "::" + qual.substr(qual.rfind(':') + 1) +
             ";\n";
        d += "    void* __wcs_ctx{nullptr};\n";
        d += "    std::uint64_t __wcs_mask{0};\n";
        std::string overrides{};
        std::size_t k{0};
        template for (constexpr auto slot : slots) {
            const std::string ks{std::to_string(k)};
            const std::string idx{idx_base + ks + ")"};
            static constexpr const char* sname{std::define_static_string(
                std::meta::identifier_of(slot))};
            const std::string name{sname};
            if constexpr (!director_slot_supported(slot)) {
                overrides += "    static_assert(false, \"welder: the virtual '" +
                             qual + "::" + name +
                             "' has a shape the C# director wire cannot carry "
                             "(C-variadic, or a reference/pointer class or "
                             "string return); mark it "
                             "[[=welder::bind_flat]] to bind it "
                             "non-overridably\");\n";
                ++k;
                continue;
            }
            // table entry: <wire-ret> (*sK)(void*, wires..., welder_error*)
            std::string wires{};
            template for (constexpr auto p : std::define_static_array(
                              std::meta::parameters_of(slot))) {
                wires += ", ";
                wires += wire_param_v<std::meta::type_of(p)>;
            }
            tbl += "        " +
                   std::string{
                       wire_return_v<std::meta::return_type_of(slot)>} +
                   " (*s" + ks + ")(void*" + wires +
                   ", welder_error*);\n";

            // the override: spliced signature, callback-or-qualified-base body
            overrides += "    [: ::std::meta::return_type_of(" + idx + ") :] " +
                         name + "(";
            std::string cargs{};      // the C++ argument names
            std::string wire_args{};  // ctx + converted wire args
            {
                [[maybe_unused]] std::size_t j{0};
                template for ([[maybe_unused]] constexpr auto p :
                              std::define_static_array(
                                  std::meta::parameters_of(slot))) {
                    const std::string js{std::to_string(j)};
                    if (j) {
                        overrides += ", ";
                        cargs += ", ";
                    }
                    const std::string pt{"::std::meta::type_of(::std::meta::"
                                         "parameters_of(" +
                                         idx + ")[" + js + "])"};
                    overrides += "[: " + pt + " :] a" + js;
                    cargs += "a" + js;
                    wire_args += ", wcs::shim::to_wire_arg<" + pt + ">(a" + js +
                                 ").get()";
                    ++j;
                }
            }
            static constexpr const char* quals{
                std::define_static_string(slot_qualifiers(slot))};
            overrides += ")" + std::string{quals} + " override {\n";
            overrides += "        if (__wcs_ctx && (__wcs_mask & (1ull << " +
                         ks + ")) && __wcs_tbl.s" + ks + ") {\n";
            overrides += "            welder_error __e{0, nullptr};\n";
            constexpr bool voidret{
                classify(std::meta::return_type_of(slot)) ==
                marshal_kind::void_};
            if constexpr (voidret) {
                overrides += "            __wcs_tbl.s" + ks + "(__wcs_ctx" +
                             wire_args + ", &__e);\n";
                overrides += "            if (__e.code != 0) "
                             "wcs::shim::rethrow_managed(&__e);\n";
                overrides += "            return;\n";
            } else {
                overrides += "            auto __r = __wcs_tbl.s" + ks +
                             "(__wcs_ctx" + wire_args + ", &__e);\n";
                overrides += "            if (__e.code != 0) "
                             "wcs::shim::rethrow_managed(&__e);\n";
                overrides += "            return wcs::shim::from_wire_return<"
                             "::std::meta::return_type_of(" +
                             idx + ")>(__r);\n";
            }
            overrides += "        }\n";
            if constexpr (std::meta::is_pure_virtual(slot)) {
                overrides += "        throw std::runtime_error{\"welder: pure "
                             "virtual '" + name +
                             "' called with no managed override\"};\n";
            } else {
                overrides += "        return " + qual + "::" + name + "(" +
                             cargs + ");\n";
            }
            overrides += "    }\n";

            // Record the slot so add_method can match its callables (the
            // method sweep has no compile-time handle on the welded type).
            static constexpr const char* vsig{std::define_static_string(
                std::meta::display_string_of(std::meta::type_of(slot)))};
            w.vslots.push_back(class_writer::vslot{sname, vsig, k});
            ++k;
        }
        tbl += "    };\n    static inline __wcs_table_t __wcs_tbl{};\n";
        d += tbl;
        d += "    ~" + dir + "() override { if (__wcs_ctx && __wcs_tbl.release) "
             "__wcs_tbl.release(__wcs_ctx); }\n";
        d += overrides;
        d += "};\n\n";
        m.doc->directors += d;

        // --- registration + bind thunks -----------------------------------
        const std::string init_sym{w.sym_prefix + "_dir_init"};
        const std::string bind_sym{w.sym_prefix + "_dir_bind"};
        m.doc->record_symbol(init_sym);
        m.doc->record_symbol(bind_sym);
        std::string init_params{"void* release"};
        std::string init_body{"    " + dir +
                              "::__wcs_tbl.release = "
                              "reinterpret_cast<void (*)(void*)>(release);\n"};
        std::string cs_init_params{"IntPtr release"};
        std::string cs_init_args{
            "                (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, "
            "void>)&__Release"};
        std::string cs_slots{};      // the [UnmanagedCallersOnly] thunks
        std::string cs_mask{};       // the __OverrideMask body lines
        k = 0;
        template for (constexpr auto slot : slots) {
            // Only a slot that is BOUND for cs gets a callback + mask entry: a
            // protected NVI hook or an excluded virtual keeps its table field
            // null, so the director falls through to the qualified base.
            if constexpr (director_slot_supported(slot) &&
                          std::meta::is_public(slot) &&
                          ::welder::member_bound(
                              slot, lang::cs,
                              ::welder::policy_of(
                                  std::meta::dealias(^^T)))) {
                const std::string ks{std::to_string(k)};
                std::string wires{};
                std::string cs_wires{};      // C# fnptr generic args (params)
                std::string cs_params{};     // __SlotK parameter list
                std::string conv{};          // arg conversions
                std::string call_args{};
                std::string typeofs{};       // __OverrideMask GetMethod types
                std::size_t j{0};
                template for (constexpr auto p : std::define_static_array(
                                  std::meta::parameters_of(slot))) {
                    const std::string js{std::to_string(j)};
                    const std::string sep{j ? ", " : ""};
                    wires += ", ";
                    wires += wire_param_v<std::meta::type_of(p)>;
                    constexpr marshal_kind pk{classify(std::meta::type_of(p))};
                    std::string cst{};
                    if constexpr (pk == marshal_kind::boolean)
                        cst = "byte";
                    else if constexpr (pk == marshal_kind::scalar) {
                        static constexpr const char* csc{
                            scalar_spell(std::meta::type_of(p)).cs};
                        cst = csc;
                    } else if constexpr (pk == marshal_kind::enum_)
                        cst = type_ref<bare(std::meta::type_of(p))>();
                    else
                        cst = "IntPtr"; // string / handle
                    cs_wires += cst + ", ";
                    cs_params += sep + cst + " a" + js;
                    call_args += sep;
                    if constexpr (pk == marshal_kind::boolean) {
                        call_args += "a" + js + " != 0";
                        typeofs += sep + "typeof(bool)";
                    } else if constexpr (pk == marshal_kind::utf8_string) {
                        conv += "                string __a" + js +
                                " = Marshal.PtrToStringUTF8(a" + js +
                                ") ?? \"\";\n";
                        call_args += "__a" + js;
                        typeofs += sep + "typeof(string)";
                    } else if constexpr (pk == marshal_kind::handle) {
                        conv += "                var __a" + js + " = new " +
                                type_ref<bare(std::meta::type_of(p))>() +
                                "(a" + js + ", false);\n";
                        call_args += "__a" + js;
                        typeofs += sep + "typeof(" +
                                   type_ref<bare(std::meta::type_of(p))>() +
                                   ")";
                    } else {
                        call_args += "a" + js;
                        typeofs += sep + "typeof(" + cst + ")";
                    }
                    ++j;
                }
                init_params += ", void* s" + ks;
                init_body += "    " + dir + "::__wcs_tbl.s" + ks +
                             " = reinterpret_cast<" +
                             std::string{wire_return_v<
                                 std::meta::return_type_of(slot)>} +
                             " (*)(void*" + wires + ", welder_error*)>(s" +
                             ks + ");\n";
                cs_init_params += ", IntPtr s" + ks;
                constexpr marshal_kind rk{
                    classify(std::meta::return_type_of(slot))};
                std::string cs_ret{};
                if constexpr (rk == marshal_kind::void_) cs_ret = "void";
                else if constexpr (rk == marshal_kind::boolean) cs_ret = "byte";
                else if constexpr (rk == marshal_kind::scalar) {
                    static constexpr const char* csr{
                        scalar_spell(std::meta::return_type_of(slot)).cs};
                    cs_ret = csr;
                } else if constexpr (rk == marshal_kind::enum_)
                    cs_ret = type_ref<bare(std::meta::return_type_of(slot))>();
                else cs_ret = "IntPtr";
                cs_init_args += ",\n                (IntPtr)(delegate* "
                                "unmanaged[Cdecl]<IntPtr, " +
                                cs_wires + "WelderError*, " +
                                (cs_ret == "void" ? "void" : cs_ret) +
                                ">)&__Slot" + ks;
                // the method-name placeholder add_method resolves at render
                static constexpr const char* ssig{std::define_static_string(
                    std::meta::display_string_of(std::meta::type_of(slot)))};
                const std::string mname{
                    "\x01" +
                    std::string{cpp_name_v<std::meta::parent_of(slot)>} + "#" +
                    std::string{ident_v<slot>} + "#" + ssig + "\x02"};
                cs_slots +=
                    "        [UnmanagedCallersOnly(CallConvs = new[] { "
                    "typeof(CallConvCdecl) })]\n"
                    "        private static unsafe " + cs_ret + " __Slot" + ks +
                    "(IntPtr __ctx" + (cs_params.empty() ? "" : ", " + cs_params) +
                    ", WelderError* __err)\n        {\n"
                    "            try\n            {\n"
                    "                var __self = (" + w.cs_name +
                    "?)GCHandle.FromIntPtr(__ctx).Target;\n"
                    "                if (__self is null) throw new "
                    "InvalidOperationException(\"welder: director target "
                    "collected\");\n" + conv;
                const std::string mcall{"__self." + mname + "(" + call_args +
                                        ")"};
                if constexpr (rk == marshal_kind::void_) {
                    cs_slots += "                " + mcall + ";\n";
                } else if constexpr (rk == marshal_kind::boolean) {
                    cs_slots += "                return (byte)(" + mcall +
                                " ? 1 : 0);\n";
                } else if constexpr (rk == marshal_kind::utf8_string) {
                    cs_slots += "                return "
                                "NativeMethods.welder_dup_utf8(" + mcall +
                                ");\n";
                } else if constexpr (rk == marshal_kind::handle) {
                    // Clone through the return class's copy thunk so the copy
                    // exists before the managed temporary can be collected.
                    cs_slots += "                var __ret = " + mcall + ";\n";
                    cs_slots += "                IntPtr __c = "
                                "NativeMethods.welder_" +
                                std::string{upath_v<bare(
                                    std::meta::return_type_of(slot))>} +
                                "_clone(__ret._h_" +
                                field_ref<bare(std::meta::return_type_of(slot))>() +
                                ", out WelderError __e2);\n"
                                "                "
                                "WelderInterop.ThrowIfError(in __e2);\n"
                                "                GC.KeepAlive(__ret);\n"
                                "                return __c;\n";
                } else {
                    cs_slots += "                return " + mcall + ";\n";
                }
                cs_slots +=
                    "            }\n            catch (Exception __ex)\n"
                    "            {\n"
                    "                __err->Code = 7;\n"
                    "                __err->Message = "
                    "NativeMethods.welder_dup_utf8(__ex.Message);\n" +
                    std::string{cs_ret == "void"
                                    ? "                return;\n"
                                    : "                return default;\n"} +
                    "            }\n        }\n";
                cs_mask += "            if (__NotWrapper(__t.GetMethod(\"" +
                           mname + "\", new Type[] { " + typeofs +
                           " })?.DeclaringType)) __m |= 1UL << " + ks + ";\n";
            }
            ++k;
        }
        m.doc->shim += "void " + init_sym + "(" + init_params + ") {\n" +
                       init_body + "}\n\n";
        m.doc->shim += "void " + bind_sym +
                       "(void* self, void* ctx, std::uint64_t mask) {\n"
                       "    if (auto* __d = dynamic_cast<" + dir +
                       "*>(reinterpret_cast<" + qual +
                       "*>(self))) { __d->__wcs_ctx = ctx; __d->__wcs_mask = "
                       "mask; }\n}\n\n";
        m.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial "
                          "void " + init_sym + "(" + cs_init_params + ");\n";
        m.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial "
                          "void " + bind_sym +
                          "(IntPtr self, IntPtr ctx, ulong mask);\n";

        // --- the managed scaffolding --------------------------------------
        std::string anc{};
        static constexpr auto ancestors{std::define_static_array(
            welded_ancestors(std::meta::dealias(^^T)))};
        template for (constexpr auto a : ancestors) {
            anc += " && __d != typeof(" + type_ref<std::meta::dealias(a)>() +
                   ")";
        }
        w.members +=
            "        private static bool __cbInit;\n"
            "        private static unsafe void __EnsureCallbacks()\n"
            "        {\n            if (__cbInit) return;\n"
            "            __cbInit = true;\n"
            "            NativeMethods." + init_sym + "(\n" + cs_init_args +
            ");\n        }\n"
            "        [UnmanagedCallersOnly(CallConvs = new[] { "
            "typeof(CallConvCdecl) })]\n"
            "        private static void __Release(IntPtr ctx) => "
            "GCHandle.FromIntPtr(ctx).Free();\n" +
            cs_slots +
            "        private static ulong __OverrideMask(Type __t)\n"
            "        {\n            ulong __m = 0;\n"
            "            if (__t == typeof(" + w.cs_name +
            ")) return __m;\n" + cs_mask +
            "            return __m;\n        }\n"
            "        private static bool __NotWrapper(Type? __d) =>\n"
            "            __d is not null && __d != typeof(" + w.cs_name + ")" +
            anc + ";\n"
            "        private void __DirBind()\n        {\n"
            "            __isDirector = true;\n"
            "            __EnsureCallbacks();\n"
            "            NativeMethods." + bind_sym + "(\n                " +
            w.handle_field + ".DangerousGetHandle(),\n                "
            "GCHandle.ToIntPtr(GCHandle.Alloc(this, GCHandleType.Weak)),\n"
            "                __OverrideMask(GetType()));\n"
            "            GC.KeepAlive(this);\n        }\n\n";
    }

  public:

    // --- enum binding -------------------------------------------------------

    template <class E>
    static enum_writer make_enum(module_type& m, const char* name,
                                 const char* doc) {
        enum_writer w{};
        w.doc = m.doc;
        w.cs_name = name;
        w.doc_text = doc ? doc : "";
        m.doc->record_type_name(cpp_name_v<std::meta::dealias(^^E)>, name);
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
            _collect_containers<fn>(*m.doc);
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
        if constexpr (classify(std::meta::type_of(Var)) == marshal_kind::seq_ref)
            _ensure_vector<bare(std::meta::type_of(Var))>(*m.doc);
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
        constexpr bool unsafe_var{classify(std::meta::type_of(Var)) ==
                                  marshal_kind::seq_value};
        body += "        public static " +
                std::string{unsafe_var ? "unsafe " : ""} +
                public_type<std::meta::type_of(Var), Style>() + " " + vname +
                "\n        {\n            get\n            {\n";
        body += wrapper_return_body<std::meta::type_of(Var), Style>(
            "NativeMethods." + base + "_get(out WelderError __e)",
            "                ");
        body += "            }\n";
        if constexpr (!read_only) {
            call_pieces vcp{};
            append_one_param<std::meta::type_of(Var), Style>(vcp, 0, "value");
            body += "            set\n            {\n" +
                    (vcp.pin_open.empty()
                         ? std::string{}
                         : "                " + vcp.pin_open + "{\n") +
                    "                NativeMethods." + base + "_set(" +
                    vcp.wrapper_args + ", out WelderError __e);\n"
                    "                WelderInterop.ThrowIfError(in __e);\n" +
                    (vcp.pin_open.empty() ? std::string{}
                                          : "                }\n") +
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
