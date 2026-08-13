#pragma once
#include <cstddef>
#include <meta>
#include <string>
#include <utility>
#include <vector>

#include <welder/doc.hpp>                         // doc_of / param_docs / return_doc_of
#include <welder/rods/csharp/document.hpp>
#include <welder/rods/csharp/emit/params.hpp>
#include <welder/rods/csharp/emit/returns.hpp>
#include <welder/rods/csharp/emit/spellings.hpp>
#include <welder/rods/csharp/text.hpp>
#include <welder/rods/csharp/type_map.hpp>

/** @file
    **The three-way emission component.** Every bound callable — method, static
    method, free function, operator, constructor — is written out as the same
    triple, keyed by one C symbol: a native thunk (into the document's shim
    buffer), a `[LibraryImport]` declaration (into its P/Invoke buffer), and a
    managed wrapper (into whichever body the caller's @ref
    welder::rods::csharp::bound_symbol was bound to).

    @ref welder::rods::csharp::callable_emitter is the component that writes
    that triple. Emitting all three from one object, over one
    @ref welder::rods::csharp::bound_symbol, is what keeps the two artifacts in
    lockstep: there is no way to add a thunk and forget its declaration, and a
    colliding symbol is caught centrally when the `bound_symbol` registers.
*/

namespace welder::inline v0::rods::csharp {

/** Emit the XML doc block for callable @a Fn above its wrapper: `<summary>`
    from its `[[=welder::doc]]`, one `<param>` per documented parameter (keyed
    by the C# parameter names in @a cp) and `<returns>` from
    `[[=welder::returns]]`.
    @tparam Fn  a reflection of the callable.
    @param out the buffer the doc block is appended to.
    @param ind the indentation of each `///` line.
    @param cp  the callable's parameter pieces (source of the C# names the
               `<param>` tags key on). */
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

/** The component that emits one callable overload as its coordinated triple —
    native thunk, `[LibraryImport]` declaration, managed wrapper — over one
    @ref bound_symbol.

    Construction gathers everything the three fragments share (the parameter
    pieces, the resolved names, the optional instance shape); @ref emit then
    writes the three artifacts. The class replaces a free function that
    threaded five positional strings — each contextual string is now a named
    member, and each artifact a named private step.
    @tparam Fn    a reflection of the bound callable.
    @tparam Style the name style (parameter identifiers restyle through it). */
template <std::meta::info Fn, class Style>
class callable_emitter {
  public:
    /** Prepare a STATIC emission (a static method or free function — no
        instance argument).
        @param sym          the callable's registered symbol and sinks.
        @param wrapper_name the wrapper's resolved C# name.
        @param delegate_expr the support-template instantiation the thunk
               delegates into, e.g. `wcs::shim::function<wcs::named_member(…)>`
               — the thunk body appends `(err, a0, …)`. */
    callable_emitter(bound_symbol sym, std::string wrapper_name,
                     std::string delegate_expr)
        : sym_{std::move(sym)}, name_{std::move(wrapper_name)},
          expr_{std::move(delegate_expr)}, has_self_{false} {}

    /** Prepare an INSTANCE emission (a leading `void* self` on the thunk, the
        class's `SafeHandle` on the P/Invoke, `this`' handle field on the call).
        @param sym          the callable's registered symbol and sinks.
        @param wrapper_name the wrapper's resolved C# name.
        @param delegate_expr the support-template instantiation the thunk
               delegates into — the thunk body appends `(self, err, a0, …)`.
        @param self_cs      the handle class's C# type spelling.
        @param self_field   the wrapper's handle-field expression. */
    callable_emitter(bound_symbol sym, std::string wrapper_name,
                     std::string delegate_expr, std::string self_cs,
                     std::string self_field)
        : sym_{std::move(sym)}, name_{std::move(wrapper_name)},
          expr_{std::move(delegate_expr)}, self_cs_{std::move(self_cs)},
          self_field_{std::move(self_field)}, has_self_{true} {}

    /** Write the triple: thunk, P/Invoke declaration, wrapper. Also the home
        of the per-callable generation-time gates (return-policy validity, the
        marshallability phase gate), so no caller can bypass them. */
    void emit() {
        ::welder::validate_return_policy<Fn, lang::cs>();
        constexpr bool ret_checked{
            (require_marshallable(std::meta::return_type_of(Fn), true), true)};
        static_assert(ret_checked);
        emit_thunk();
        emit_pinvoke();
        emit_wrapper();
    }

  private:
    /** The reflected return type, the axis every fragment branches on. */
    static constexpr std::meta::info R{std::meta::return_type_of(Fn)};

    /** Write the native thunk: a one-line delegation into the compiled
        marshalling library, parameterized by the exact member reflection. */
    void emit_thunk() {
        std::string shim_params{cp_.shim_params};
        if (has_self_)
            shim_params = "void* self" +
                          (cp_.shim_params.empty() ? "" : ", " + cp_.shim_params);
        shim_params += (shim_params.empty() ? "" : ", ");
        shim_params += "welder_error* err";
        std::string delegate_args{has_self_ ? "self, err" : "err"};
        if (!cp_.delegate_args.empty())
            delegate_args += ", " + cp_.delegate_args;
        code_writer t{sym_.thunk()};
        t.line("{} {}({}) { return {}({}); }", wire_return_v<R>, sym_.name(),
               shim_params, expr_, delegate_args);
        t.blank();
    }

    /** Write the `[LibraryImport]` declaration (UTF-8 marshalling when any
        string crosses; `[return: MarshalAs(U1)]` for a `bool` return). */
    void emit_pinvoke() {
        std::string pin_params{cp_.pinvoke_params};
        if (has_self_)
            pin_params = self_cs_ + " self" +
                         (cp_.pinvoke_params.empty() ? ""
                                                     : ", " + cp_.pinvoke_params);
        pin_params += (pin_params.empty() ? "" : ", ");
        pin_params += "out WelderError err";
        constexpr bool r_is_bool{classify(R) == marshal_kind::boolean};
        sym_.pinvoke().line(
            "{} {}internal static partial {} {}({});",
            import_attr(cp_.has_string),
            r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "",
            pinvoke_type<R, Style>(true), sym_.name(), pin_params);
    }

    /** Write the managed wrapper: XML docs, the signature, and the return
        path (@ref wrapper_return_body) inside whatever staging the parameter
        list demands (@ref call_pieces::wrap). */
    void emit_wrapper() {
        code_writer w{sym_.wrapper()};
        std::string call_args{has_self_ ? self_field_ : std::string{}};
        if (!cp_.wrapper_args.empty())
            call_args += (call_args.empty() ? "" : ", ") + cp_.wrapper_args;
        call_args += (call_args.empty() ? "" : ", ");
        call_args += "out WelderError _e";
        const std::string pc{"NativeMethods." + sym_.name() + "(" + call_args +
                             ")"};
        std::string docs{};
        emit_callable_docs<Fn>(docs, w.indentation(), cp_);
        w.raw(docs);
        constexpr bool ret_unsafe{classify(R) == marshal_kind::seq_value ||
                                  classify(R) == marshal_kind::tuple_value};
        w.line("public {}{}{} {}({})",
               cp_.needs_unsafe || ret_unsafe ? "unsafe " : "",
               has_self_ ? "" : "static ", public_return_type<R, Style>(),
               name_, cp_.wrapper_params);
        {
            const auto body{w.braces()};
            w.raw(cp_.wrap(
                wrapper_return_body<R, Style,
                                    ::welder::return_policy_of(Fn, lang::cs)>(
                    pc, w.indentation() + (cp_.post.empty() ? "" : "    "),
                    has_self_ ? "this" : ""),
                w.indentation()));
        }
        w.blank();
    }

    bound_symbol sym_;      /**< The symbol and its three coordinated sinks. */
    std::string name_;      /**< The wrapper's resolved C# name. */
    std::string expr_;      /**< The thunk's delegate expression. */
    std::string self_cs_;   /**< Instance form: the handle class's C# type. */
    std::string self_field_;/**< Instance form: the handle-field expression. */
    bool has_self_;         /**< Whether this is an instance emission. */
    /** The parameter list in its five spellings (built once, shared by all
        three fragments). */
    call_pieces cp_{build_params<Fn, Style>(
        std::make_index_sequence<std::meta::parameters_of(Fn).size()>{})};
};

} // namespace welder::inline v0::rods::csharp
