#pragma once
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/csharp/directors.hpp>
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/directors.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **Opening a class or enum**: establishing the four identities every later
    hook depends on, and emitting the scaffolding that belongs to the type
    itself rather than to any one member.

    A welded type is known by four names at once, and getting them right is most
    of what these functions do:

    | Identity | Used by |
    |---|---|
    | the C# path from the root namespace | every managed reference |
    | the `::`-qualified C++ spelling | the shim's `^^…` anchors |
    | the `welder_<path>` C-symbol prefix | every emitted thunk |
    | the handle field / handle class | every P/Invoke that passes the object |

    For an **alias-welded specialization** the C++ spelling comes from the
    welding alias (`Decl`), not from the type — a specialization has no
    qualified name of its own — and both spellings are registered in the
    document's registries so that references keyed either way resolve. A NESTED
    type spells through its outer's anchor for the same reason, and a
    **protected** nested type spells through reflection enumeration, its
    qualified name being inaccessible at the shim's namespace scope.

    @ref welder::rods::csharp::finish_class then emits what the type owns: the
    base upcast thunks (a compiled `static_cast`, so multiple/virtual
    inheritance offsets are the ABI's own), the extra bases' `As<Base>()` views,
    the director apparatus when eligible, and the destroy thunk backing the
    `SafeHandle`.
*/

namespace welder::inline v0::rods::csharp {

/** The declaring-entity-aware form the carriage prefers: @a Decl is `^^T`,
    or the namespace-scope **alias** a class-template specialization was
    welded through — the one C++-spellable anchor such a target has, which
    the emitted shim's `^^…` spellings need. */
template <class T, std::meta::info Decl, auto Bases>
class_writer open_class(module_writer& m, const char* name, const char* doc) {
    class_writer w{};
    w.doc = m.doc;
    w.cs_name = name;
    w.cs_ns = m.cs_ns;
    // The dotted path FROM THE ROOT namespace: what every cross-namespace
    // reference (P/Invoke handle types, base classes, views) spells — valid
    // from anywhere inside the root namespace, C# lookup walks outward.
    w.cs_path = m.cs_ns.empty() ? std::string{name} : m.cs_ns + "." + name;
    w.doc_text = doc ? doc : "";
    w.cpp_qualified = cpp_name_v<Decl>;
    w.cpp_anchor = "^^" + w.cpp_qualified;
    // References to T anywhere (params, returns, fields, operators) are
    // emitted as placeholders over the RAW spelling; register the final
    // name for the render-time reconciliation. The reference spelling is
    // ^^T's own (what type_ref emits), which for an alias-welded
    // specialization differs from Decl's — record both.
    m.doc->record_type_name(cpp_name_v<Decl>, w.cs_path);
    // The SHIM's C++ spelling for this type, under the same keys. Decl is
    // the spellable one (the welding alias for a specialization), so it is
    // the anchor everything downstream must splice. @see anchor_ref
    m.doc->record_type_anchor(cpp_name_v<Decl>, w.cpp_qualified);
    if constexpr (spellable(std::meta::dealias(^^T))) {
        if (std::string{cpp_name_v<std::meta::dealias(^^T)>} !=
            cpp_name_v<Decl>) {
            m.doc->record_type_name(cpp_name_v<std::meta::dealias(^^T)>,
                                    w.cs_path);
            m.doc->record_type_anchor(cpp_name_v<std::meta::dealias(^^T)>,
                                      w.cpp_qualified);
        }
    } else {
        // An alias-welded specialization: references key on the display
        // string (see type_ref). This is THE case the anchor registry
        // exists for — the display string is all a container generator has,
        // and it is not a spelling the shim can use.
        static constexpr const char* d{std::define_static_string(
            std::meta::display_string_of(std::meta::dealias(^^T)))};
        m.doc->record_type_name(d, w.cs_path);
        m.doc->record_type_anchor(d, w.cpp_qualified);
    }
    w.sym_prefix = std::string{"welder_"} + upath_v<Decl>;
    // Same identifier-safe rule as the nested factory: the handle FIELD
    // sanitizes the dotted path (matching field_ref's placeholder flavor),
    // the handle TYPE reference is the from-root dotted spelling (the
    // declaration itself uses the leaf, beside the class).
    std::string safe{w.cs_path};
    for (char& c : safe)
        if (c == '.')
            c = '_';
    w.handle_field = "_h_" + safe;
    w.handle_cs = w.cs_path + "Handle";
    w.destroy_symbol = w.sym_prefix + "_destroy";
    finish_class<T, Bases>(m, w);
    return w;
}
/** The class-emission core shared by the flat and nested factories: base
    upcast thunks / As-views, the director apparatus and the destroy
    thunk — everything that depends on the writer's already-set identities
    (`cpp_qualified` / `sym_prefix` — the nested factory derives those from
    the OUTER's alias anchor, since a specialization's own qualified name
    is unspellable). */
template <class T, auto Bases>
void finish_class(module_writer& m, class_writer& w) {
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
                       "wcs::shim::upcast<" + w.cpp_anchor + ", ^^" +
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
                         "            IntPtr _p = NativeMethods." + bsym +
                         "(" + w.handle_field + ".DangerousGetHandle());\n"
                         "            GC.KeepAlive(this);\n"
                         "            var _v = new " + bref +
                         "(_p, false);\n"
                         "            _v._owner = this;\n"
                         "            return _v;\n        }\n\n";
        }
        ++base_i;
    }
    if constexpr (director_eligible(std::meta::dealias(^^T))) {
        w.is_director = true;
        // "welder_" (7 chars incl. underscore) -> "welder_dir_..."
        w.director_ident = "welder_dir_" + w.sym_prefix.substr(7);
        emit_director<T>(m, w);
    }
    // The destructor thunk + its P/Invoke (the SafeHandle's release path).
    m.doc->record_symbol(w.destroy_symbol);
    m.doc->shim += "void " + w.destroy_symbol +
                   "(void* self) { wcs::shim::destroy<" + w.cpp_anchor +
                   ">(self); }\n\n";
    m.doc->pinvoke += "        [LibraryImport(Lib)] internal static partial void " +
                      w.destroy_symbol + "(IntPtr self);\n";
}
/** The declaring-entity-aware nested form the carriage prefers. The C++
    anchor spells THROUGH the outer's own anchor (`IntCrate::Lid`) — a
    member type of an alias-welded specialization has no other spellable
    qualified name — and the symbols chain off the outer's prefix, so two
    instantiations' same-named nested types cannot collide. */
template <class T, std::meta::info Decl, auto Bases>
class_writer open_nested_class(module_writer& m, class_writer& outer,
                               const char* name, const char* doc) {
    outer.nested_names.push_back(name);
    class_writer w{};
    w.doc = m.doc;
    w.cs_name = name;
    w.doc_text = doc ? doc : "";
    // The nested segment's spelling comes from the DECLARED member — the
    // nested class itself, or the class-scope ALIAS an unspellable vendor
    // target (a specialization) was registered through.
    static constexpr const char* tid{
        std::define_static_string(std::meta::identifier_of(Decl))};
    w.cpp_qualified = outer.cpp_qualified + "::" + tid;
    // A PUBLIC nested type's qualified name is its anchor; a protected
    // one is inaccessible at namespace scope, so the shim re-derives its
    // reflection by enumeration instead of spelling it.
    if constexpr (std::meta::is_public(Decl))
        w.cpp_anchor = "^^" + w.cpp_qualified;
    else
        w.cpp_anchor = "std::meta::dealias(wcs::nested_type(" +
                       outer.cpp_anchor + ", \"" + tid + "\"))";
    w.sym_prefix = outer.sym_prefix + "_" + tid;
    w.destroy_symbol = w.sym_prefix + "_destroy";
    w.sink = &outer.members;
    w.cs_path = outer.cs_path + "." + name;
    w.handle_cs = w.cs_path + "Handle";
    std::string safe{w.cs_path};
    for (char& c : safe)
        if (c == '.')
            c = '_';
    w.handle_field = "_h_" + safe;
    // Register the dotted path under every spelling a reference can key
    // on: the anchor, and — spellable — ^^T's own name, else the display
    // string (a nested type of a specialization; see type_ref).
    m.doc->record_type_name(w.cpp_qualified, w.cs_path);
    if constexpr (spellable(std::meta::dealias(^^T))) {
        m.doc->record_type_name(cpp_name_v<std::meta::dealias(^^T)>,
                                w.cs_path);
    } else {
        static constexpr const char* d{std::define_static_string(
            std::meta::display_string_of(std::meta::dealias(^^T)))};
        m.doc->record_type_name(d, w.cs_path);
    }
    finish_class<T, Bases>(m, w);
    return w;
}
template <class E>
enum_writer open_enum(module_writer& m, const char* name, const char* doc) {
    enum_writer w{};
    w.doc = m.doc;
    w.cs_name = name;
    w.cs_ns = m.cs_ns;
    w.doc_text = doc ? doc : "";
    m.doc->record_type_name(cpp_name_v<std::meta::dealias(^^E)>,
                            m.cs_ns.empty() ? std::string{name}
                                            : m.cs_ns + "." + name);
    constexpr const char* u{scalar_spell(
        std::meta::underlying_type(std::meta::dealias(^^E))).cs};
    w.underlying = u;
    return w;
}
/** Place a NESTED member enum under the outer's binding (same model). */
template <class E>
enum_writer open_nested_enum(module_writer& m, class_writer& outer,
                             const char* name, const char* doc) {
    outer.nested_names.push_back(name);
    enum_writer w{open_enum<E>(m, name, doc)};
    w.sink = &outer.members;
    // Same key rules as the nested class factory: spellable name, else
    // the display string (an enum nested in a specialization).
    if constexpr (spellable(std::meta::dealias(^^E))) {
        m.doc->record_type_name(cpp_name_v<std::meta::dealias(^^E)>,
                                outer.cs_path + "." + name);
    } else {
        static constexpr const char* d{std::define_static_string(
            std::meta::display_string_of(std::meta::dealias(^^E)))};
        m.doc->record_type_name(d, outer.cs_path + "." + name);
    }
    return w;
}
} // namespace welder::inline v0::rods::csharp
