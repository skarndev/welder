#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <utility>

#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/emit/tuples.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    The **managed return path**: the statements that turn a checked P/Invoke
    result into the value the wrapper's caller receives.

    @ref welder::rods::csharp::wrapper_return_body is the counterpart of the
    shim's `guarded` — same taxonomy, opposite direction — and it is where the
    return-value policy becomes visible managed-side: an owned kind wraps with
    `owns: true`, a view with `owns: false`, and `reference_internal`
    additionally stores the owner in the view's `_owner` so the parent cannot be
    collected while the view lives.

    Every branch emits the error check immediately after the call, so no
    generated call site can forget it.
*/

namespace welder::inline v0::rods::csharp {

/** The wrapper statements converting the checked P/Invoke result into the
    managed return value. @a pc is the P/Invoke call expression (without the
    trailing error arg — appended here so every call is checked), @a ind the
    indentation. A welded-class result follows @ref handle_return_of for
    policy @a Rv: owned kinds wrap with `owns: true`; view kinds with
    `owns: false`, and `view_keepalive` additionally stores @a owner in the
    view's `_owner` (preventing collection of the parent while the view
    lives — the managed spelling of `reference_internal`). */
template <std::meta::info R, class Style,
          ::welder::rv_kind Rv = ::welder::rv_kind::automatic>
std::string wrapper_return_body(const std::string& pc, const std::string& ind,
                                const std::string& owner = {}) {
    constexpr marshal_kind k{classify(R)};
    const std::string check{ind + "WelderInterop.ThrowIfError(in _e);\n"};
    if constexpr (k == marshal_kind::void_)
        return ind + pc + ";\n" + check;
    else if constexpr (k == marshal_kind::utf8_string)
        return ind + "IntPtr _r = " + pc + ";\n" + check +
               ind + "try { return Marshal.PtrToStringUTF8(_r) ?? \"\"; }\n" +
               ind + "finally { NativeMethods.welder_free(_r); }\n";
    else if constexpr (k == marshal_kind::shared_ptr_) {
        std::string out{ind + "var _r = " + pc + ";\n" + check};
        out += ind + "if (_r.Obj == IntPtr.Zero) return null;\n";
        out += ind + "var _v = new " +
               type_ref<bare(std::meta::template_arguments_of(bare(R))[0])>() +
               "(_r.Obj, false);\n";
        // the boxed shared_ptr copy pins the object for the view's lifetime
        out += ind + "_v._owner = new " +
               field_ref<bare(std::meta::template_arguments_of(bare(R))[0])>() +
               "SharedBox(_r.Box, true);\n";
        out += ind + "return _v;\n";
        return out;
    } else if constexpr (k == marshal_kind::unique_ptr_) {
        std::string out{ind + "IntPtr _r = " + pc + ";\n" + check};
        out += ind + "if (_r == IntPtr.Zero) return null;\n";
        out += ind + "return new " +
               type_ref<bare(std::meta::template_arguments_of(bare(R))[0])>() +
               "(_r, true);\n"; // ownership transferred (release)
        return out;
    } else if constexpr (k == marshal_kind::handle ||
                       k == marshal_kind::seq_ref ||
                       k == marshal_kind::map_ref) {
        constexpr handle_return hr{handle_return_of(R, Rv)};
        std::string out{ind + "IntPtr _r = " + pc + ";\n" + check};
        if constexpr (handle_return_nullable(R))
            out += ind + "if (_r == IntPtr.Zero) return null;\n";
        if constexpr (hr == handle_return::view ||
                      hr == handle_return::view_keepalive) {
            out += ind + "var _v = new " + public_type<R, Style>() +
                   "(_r, false);\n";
            if (hr == handle_return::view_keepalive && !owner.empty())
                out += ind + "_v._owner = " + owner + ";\n";
            out += ind + "return _v;\n";
        } else {
            out += ind + "return new " + public_type<R, Style>() +
                   "(_r, true);\n";
        }
        return out;
    } else if constexpr (k == marshal_kind::optional_) {
        constexpr marshal_kind pk{classify(optional_payload(bare(R)))};
        std::string out{ind + "var _r = " + pc + ";\n" + check};
        if constexpr (pk == marshal_kind::utf8_string) {
            out += ind + "if (_r.Has == 0) return null;\n";
            out += ind + "IntPtr _s = _r.S;\n";
            out += ind + "try { return Marshal.PtrToStringUTF8(_s) ?? \"\"; }\n";
            out += ind + "finally { NativeMethods.welder_free(_s); }\n";
        } else if constexpr (pk == marshal_kind::handle) {
            out += ind + "return _r.Has != 0 ? new " +
                   type_ref<bare(optional_payload(bare(R)))>() +
                   "(_r.P, true) : null;\n";
        } else if constexpr (pk == marshal_kind::boolean) {
            out += ind + "return _r.Has != 0 ? (bool?)(_r.I != 0) : null;\n";
        } else if constexpr (pk == marshal_kind::enum_) {
            out += ind + "return _r.Has != 0 ? (" +
                   type_ref<bare(optional_payload(bare(R)))>() + "?)(" +
                   type_ref<bare(optional_payload(bare(R)))>() +
                   ")_r.I : null;\n";
        } else if constexpr (type_trait(^^std::is_floating_point_v,
                                        bare(optional_payload(bare(R))))) {
            constexpr const char* c{
                scalar_spell(optional_payload(bare(R))).cs};
            out += ind + "return _r.Has != 0 ? (" + std::string{c} +
                   "?)unchecked((" + c + ")_r.F) : null;\n";
        } else {
            constexpr const char* c{
                scalar_spell(optional_payload(bare(R))).cs};
            out += ind + "return _r.Has != 0 ? (" + std::string{c} +
                   "?)unchecked((" + c + ")_r.I) : null;\n";
        }
        return out;
    } else if constexpr (k == marshal_kind::tuple_value) {
        std::string out{ind + "var _r = " + pc + ";\n" + check};
        out += tuple_read_stmts<R>(
            ind, std::make_index_sequence<tuple_arity(bare(R))>{});
        return out;
    } else if constexpr (k == marshal_kind::seq_string) {
        // Each element is its own UTF-8 buffer; the helper reads, frees each,
        // then frees the pointer array itself.
        return ind + "var _r = " + pc + ";\n" + check + ind +
               "return WelderInterop.FromUtf8Seq(_r);\n";
    } else if constexpr (k == marshal_kind::seq_value) {
        std::string ecs{};
        if constexpr (classify(sequence_element(bare(R))) ==
                      marshal_kind::enum_)
            ecs = type_ref<bare(sequence_element(bare(R)))>();
        else {
            constexpr const char* c{scalar_spell(sequence_element(bare(R))).cs};
            ecs = c;
        }
        std::string out{ind + "var _r = " + pc + ";\n" + check};
        out += ind + "var _out = new " + ecs + "[_r.Len];\n";
        out += ind + "if (_r.Len != 0)\n" + ind + "{\n";
        out += ind + "    fixed (" + ecs + "* _d = _out)\n";
        out += ind + "        Buffer.MemoryCopy((void*)_r.Data, _d, _r.Len * "
               "sizeof(" + ecs + "), _r.Len * sizeof(" + ecs + "));\n";
        out += ind + "}\n";
        out += ind + "if (_r.Data != IntPtr.Zero) "
               "NativeMethods.welder_free(_r.Data);\n";
        out += ind + "return _out;\n";
        return out;
    } else {
        return ind + "var _r = " + pc + ";\n" + check + ind + "return _r;\n";
    }
}
} // namespace welder::inline v0::rods::csharp
