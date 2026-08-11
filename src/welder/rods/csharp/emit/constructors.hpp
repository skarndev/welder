#pragma once
#include <cstddef>
#include <meta>
#include <string>

#include <welder/bind_traits.hpp>
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/callables.hpp>
#include <welder/rods/csharp/emit/containers.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The **constructor surface**, emitted in one call: a parameterless form, one
    per admitted declared constructor, the synthesized aggregate field
    constructor, and — for a copyable type — the copy constructor as the managed
    `Clone()` (C# has no copy-constructor protocol, and a `T(other)` overload
    would collide with a one-argument user constructor).

    Two details are structural rather than cosmetic. A **director-eligible**
    type is constructed AS its director subclass, so a C# subclass can override
    its virtuals; the handle still points at the welded type, which is what
    `construct_as` adjusts. And every public constructor **chains** through the
    internal `(IntPtr, bool)` one, which is what initializes each base level's
    upcast handle — so construction works identically for roots and derived
    classes.
*/

namespace welder::inline v0::rods::csharp {

/** Emit the whole constructor surface in one call (main's contract): a
    no-argument form when @a HasDefault, one per member of @a Ctors (exact
    constructors, spliced via `ctor_at`), the aggregate field constructor
    when @a Aggregate, and — @a Copyable — the admitted copy constructor as
    the managed `Clone()` (C# has no copy-constructor protocol; a `T(other)`
    overload would collide with a one-argument user constructor). */
template <class T, auto Ctors, bool HasDefault, bool Aggregate, bool Copyable>
void emit_constructors(class_writer& w) {
    const std::string anchor{w.cpp_anchor};
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
        collect_containers<ctor>(*w.doc);
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
                     "            IntPtr _r = NativeMethods." + sym +
                     "(" + w.handle_field + ", out WelderError _e);\n"
                     "            WelderInterop.ThrowIfError(in _e);\n"
                     "            return new " + w.cs_name + "(_r, true);\n"
                     "        }\n\n";
    }
}
} // namespace welder::inline v0::rods::csharp
