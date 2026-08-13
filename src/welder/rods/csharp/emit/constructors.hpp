#pragma once
#include <cstddef>
#include <meta>
#include <string>

#include <welder/bind_traits.hpp>
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/containers.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/text.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The **constructor surface**, emitted in one pass by
    @ref welder::rods::csharp::constructor_emitter — a parameterless form, one
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

/** The component that emits a class's whole constructor surface in one call
    (the driver's `add_constructors` contract). Each admitted form goes through
    one shared per-constructor step (@ref emit_one), so the chaining rule —
    every public constructor routes through the internal `(IntPtr, bool)` one —
    is written in exactly one place. */
class constructor_emitter {
  public:
    /** Bind the emitter to class handle @a w.
        @param w the class being emitted into. */
    explicit constructor_emitter(class_writer& w) : w_{w} {}

    /** Emit the whole constructor surface: a no-argument form when
        @a HasDefault, one per member of @a Ctors (exact constructors, spliced
        via `ctor_at`), the aggregate field constructor when @a Aggregate, and
        — @a Copyable — the admitted copy constructor as the managed `Clone()`.
        @tparam T          the welded class.
        @tparam Ctors      the admitted declared-constructor group (a
                           `std::array<std::meta::info, N>`).
        @tparam HasDefault whether the (admitted) default constructor exists.
        @tparam Aggregate  whether aggregate construction participates.
        @tparam Copyable   whether the admitted copy constructor rides along. */
    template <class T, auto Ctors, bool HasDefault, bool Aggregate,
              bool Copyable>
    void emit_all() {
        const std::string anchor{w_.cpp_anchor};
        // A director-eligible type is C#-constructed AS its director subclass
        // (the handle stays "pointer to T" — construct_as adjusts), so a C#
        // subclass can override its virtuals; unoverridden slots fall through
        // to the qualified base call, so a plain C#-side instance behaves
        // identically to a T.
        const std::string dir_anchor{w_.is_director ? "^^" + w_.director_ident
                                                    : std::string{}};
        if constexpr (HasDefault) {
            emit_one(call_pieces{}, w_.sym_prefix + "_new_default",
                     w_.is_director
                         ? "wcs::shim::default_construct_as<" + dir_anchor +
                               ", " + anchor + ">"
                         : "wcs::shim::default_construct<" + anchor + ">");
        }
        template for (constexpr auto ctor : std::define_static_array(Ctors)) {
            constexpr std::size_t k{index_of_ctor(ctor)};
            constexpr std::size_t n{std::meta::parameters_of(ctor).size()};
            collect_containers<ctor>(*w_.doc);
            emit_one(build_params<ctor, ::welder::naming::none>(
                         std::make_index_sequence<n>{}),
                     w_.sym_prefix + "_new_" + std::to_string(k),
                     w_.is_director
                         ? "wcs::shim::construct_as<" + dir_anchor + ", " +
                               anchor + ", wcs::ctor_at(" + anchor + ", " +
                               std::to_string(k) + ")>"
                         : "wcs::shim::construct<" + anchor + ", wcs::ctor_at(" +
                               anchor + ", " + std::to_string(k) + ")>");
        }
        if constexpr (Aggregate) {
            constexpr std::size_t n{
                ::welder::detail::aggregate_fields<T>().size()};
            emit_one(aggregate_pieces<T, ::welder::naming::none>(
                         std::make_index_sequence<n>{}),
                     w_.sym_prefix + "_new_agg",
                     "wcs::shim::aggregate_construct<" + anchor + ">");
        }
        if constexpr (Copyable)
            emit_clone();
    }

  private:
    /** Emit one constructor from its parameter pieces: the shim delegation,
        an `IntPtr` P/Invoke, and a `public T(...)` wrapper chaining through
        the internal `(IntPtr, bool)` constructor.
        @param cp  the constructor's parameter pieces (pre-built — the exact
                   constructor reflection lives inside @a delegate_expr).
        @param sym the constructor's C symbol.
        @param delegate_expr the support-template instantiation the thunk
               delegates into (`wcs::shim::construct<…>` and friends). */
    void emit_one(const call_pieces& cp, const std::string& sym,
                  const std::string& delegate_expr) {
        const bound_symbol bs{*w_.doc, sym, w_.members, 2};
        std::string shim_params{cp.shim_params};
        shim_params += (shim_params.empty() ? "" : ", ");
        shim_params += "welder_error* err";
        std::string delegate_args{"err"};
        if (!cp.delegate_args.empty())
            delegate_args += ", " + cp.delegate_args;
        code_writer t{bs.thunk()};
        t.line("void* {}({}) { return {}({}); }", bs.name(), shim_params,
               delegate_expr, delegate_args);
        t.blank();
        std::string pin_params{cp.pinvoke_params};
        pin_params += (pin_params.empty() ? "" : ", ");
        pin_params += "out WelderError err";
        bs.pinvoke().line("{} internal static partial IntPtr {}({});",
                          import_attr(cp.has_string), bs.name(), pin_params);
        // The public constructor CHAINS through the internal (IntPtr, bool)
        // one (which initializes every base level's upcast handle), so
        // construction works identically for roots and derived classes. The
        // static helper exists because a chained `this(...)` argument cannot
        // use `out var`.
        const std::string helper{"_New" + sym.substr(sym.rfind("_new") + 4)};
        const std::string cbody{
            std::string{cp.post.empty() ? "            " : "                "} +
            "IntPtr _r = NativeMethods." + sym + "(" +
            (cp.wrapper_args.empty() ? std::string{} : cp.wrapper_args + ", ") +
            "out WelderError _e);\n" +
            (cp.post.empty() ? "            " : "                ") +
            "WelderInterop.ThrowIfError(in _e);\n" +
            (cp.post.empty() ? "            " : "                ") +
            "return _r;\n"};
        code_writer mw{bs.wrapper()};
        mw.line("private static {}IntPtr {}({})",
                cp.needs_unsafe ? "unsafe " : "", helper, cp.wrapper_params);
        {
            const auto body{mw.braces()};
            mw.raw(cp.wrap(cbody, mw.indentation()));
        }
        // Re-list the wrapper parameter NAMES for the chained call.
        std::string names{};
        for (const std::string& n : split_param_names(cp.param_names)) {
            names += (names.empty() ? "" : ", ");
            names += n;
        }
        mw.line("public {}({}) : this({}({}), true) {}", w_.cs_name,
                cp.wrapper_params, helper, names,
                w_.is_director ? "{ _DirBind(); }" : "{}");
        mw.blank();
    }

    /** Emit the admitted copy constructor as the managed `Clone()` — a clone
        thunk over the C++ copy constructor, and a `public T Clone()` wrapper.
        (Not a `T(other)` constructor overload: it would collide with a
        one-argument user constructor.) */
    void emit_clone() {
        const bound_symbol bs{*w_.doc, w_.sym_prefix + "_clone", w_.members, 2};
        code_writer t{bs.thunk()};
        t.line("void* {}(void* self, welder_error* err) { return "
               "wcs::shim::clone<{}>(self, err); }",
               bs.name(), w_.cpp_anchor);
        t.blank();
        bs.pinvoke().line(
            "[LibraryImport(Lib)] internal static partial IntPtr {}({} self, "
            "out WelderError err);",
            bs.name(), w_.handle_cs);
        code_writer mw{bs.wrapper()};
        mw.line("/// <summary>Copy this instance (the C++ copy "
                "constructor).</summary>");
        mw.line("public {} Clone()", w_.cs_name);
        {
            const auto body{mw.braces()};
            mw.line("IntPtr _r = NativeMethods.{}({}, out WelderError _e);",
                    bs.name(), w_.handle_field);
            mw.line("WelderInterop.ThrowIfError(in _e);");
            mw.line("return new {}(_r, true);", w_.cs_name);
        }
        mw.blank();
    }

    class_writer& w_; /**< The class being emitted into. */
};

} // namespace welder::inline v0::rods::csharp
