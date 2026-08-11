#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <utility>

#include <welder/bind_traits.hpp>                 // param_types / aggregate_fields
#include <welder/naming.hpp>                      // restyle
#include <welder/rods/csharp/emit/refs.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/emit/tuples.hpp>
#include <welder/rods/csharp/text.hpp>            // cs_escape
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **Parameters, in five spellings at once.**

    A single C++ parameter shows up in five different places in the two emitted
    artifacts — the shim signature, the delegation argument list, the P/Invoke
    declaration, the public wrapper signature, and the wrapper's argument
    expression — plus whatever pinning, staging or `unsafe` the marshalling of
    its type demands. @ref welder::rods::csharp::call_pieces is the record that
    carries all of them together, and
    @ref welder::rods::csharp::append_one_param is the single place that knows
    how one type fills them in.

    That is deliberately *one* conversion source: function parameters, property
    setters, operator operands and map keys/values all go through
    `append_one_param`, so an inbound conversion cannot drift between them.
*/

namespace welder::inline v0::rods::csharp {

/** The camelCase C# parameter identifier for reflection @a param (falling back
    to `p<j>` for an unnamed parameter). LEADING UNDERSCORES ARE STRIPPED —
    both the more faithful camelCase rendering of `_count`, and what keeps the
    wrapper's parameter scope disjoint from the generated `_`-prefixed locals
    (`_e`, `_r`, …), which it cannot then shadow.

    Stripping is SKIPPED where it would not leave a legal identifier: `_04` — a
    client-struct offset used as a member name — would become `04`, which C#
    rejects as a parameter name, and `__` would vanish entirely. Those keep their
    underscores; they are still legal C#, and they still cannot collide with the
    generated locals, every one of which is an underscore followed by a LETTER.
    (The old comment claimed the post-strip name always begins with a letter.
    That was an assertion, not an invariant — `_04` produced `uint 04` and the
    generated file would not parse.) */
consteval std::string param_ident(std::meta::info param, std::size_t j) {
    if (std::meta::has_identifier(param)) {
        const std::string_view raw{std::meta::identifier_of(param)};
        std::string_view id{raw};
        while (!id.empty() && id.front() == '_')
            id.remove_prefix(1);
        if (!id.empty() && !(id.front() >= '0' && id.front() <= '9'))
            return ::welder::naming::restyle(
                id, ::welder::naming::case_kind::camel);
        if (!raw.empty())
            return std::string{raw};
    }
    std::string s{"p"};
    s += static_cast<char>('0' + (j / 10) % 10);
    s += static_cast<char>('0' + j % 10);
    return j < 10 ? "p" + std::string{static_cast<char>('0' + j)} : s;
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
    std::string pre{};            /**< Statements before the call (tuple slots,
                                       staged string arrays). */
    std::string post{};           /**< Statements that must run AFTER the call
                                       whatever happens — freeing what @ref pre
                                       allocated. Emitted as a `finally`. */
    bool needs_unsafe{false};     /**< pinning / raw copies ⇒ an `unsafe` wrapper. */

    /** Wrap a call @a body in whatever staging this parameter list needs:
        the `fixed (…)` pinning block, the @ref pre statements, and — when
        anything must be released — a `try`/`finally` carrying @ref post.

        Every call site goes through this, so a parameter kind that needs
        staging cannot be silently dropped by one of them (an operator taking a
        tuple used to emit a reference to a `stackalloc` that was never
        emitted).
        @param body the call statements, already indented.
        @param ind  the indentation of the wrapping block.
        @return the wrapped statements. */
    std::string wrap(const std::string& body, const std::string& ind) const {
        std::string out{};
        if (!pin_open.empty())
            out += ind + pin_open + "{\n";
        out += indent_lines(pre, ind);
        if (post.empty()) {
            out += body;
        } else {
            out += ind + "try\n" + ind + "{\n" + body + ind + "}\n" + ind +
                   "finally\n" + ind + "{\n" +
                   indent_lines(post, ind + "    ") + ind + "}\n";
        }
        if (!pin_open.empty())
            out += ind + "}\n";
        return out;
    }

  private:
    /** Prefix every non-empty line of @a text with @a pad. @ref pre and
        @ref post are stored WITHOUT indentation precisely so that one staged
        statement reads correctly at whatever depth its call site sits.
        @param text the statements.
        @param pad  the indentation to apply.
        @return the indented statements. */
    static std::string indent_lines(const std::string& text,
                                    const std::string& pad) {
        std::string out{};
        for (std::size_t b{0}; b < text.size();) {
            std::size_t e{text.find('\n', b)};
            if (e == std::string::npos)
                e = text.size();
            if (e > b)
                out += pad + text.substr(b, e - b);
            out += '\n';
            b = e + 1;
        }
        return out;
    }
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
    } else if constexpr (classify(PT) == marshal_kind::seq_ref ||
                         classify(PT) == marshal_kind::map_ref) {
        cp.wrapper_args += name + "._h_" + container_ref<bare(PT)>();
    } else if constexpr (classify(PT) == marshal_kind::shared_ptr_) {
        cp.wrapper_args +=
            name + " is null ? IntPtr.Zero : " + name + "._h_" +
            field_ref<bare(std::meta::template_arguments_of(bare(PT))[0])>() +
            ".DangerousGetHandle()";
    } else if constexpr (classify(PT) == marshal_kind::tuple_value) {
        const std::string tw{"_tw" + i};
        cp.pre += tuple_write_stmts<PT>(
            tw, name, std::make_index_sequence<tuple_arity(bare(PT))>{});
        cp.needs_unsafe = true; // stackalloc
        cp.wrapper_args += "(IntPtr)" + tw;
    } else if constexpr (classify(PT) == marshal_kind::seq_string) {
        // A string array is not blittable: stage an unmanaged array of UTF-8
        // buffers for the call and release it in the finally.
        const std::string sw{"_sw" + i};
        cp.pre += "var " + sw + " = WelderInterop.ToUtf8Seq(" + name + ");\n";
        cp.post += "WelderInterop.FreeUtf8Seq(" + sw + ");\n";
        cp.wrapper_args += sw;
    } else if constexpr (classify(PT) == marshal_kind::seq_value) {
        const std::string pin{"_pin" + i};
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
} // namespace welder::inline v0::rods::csharp
