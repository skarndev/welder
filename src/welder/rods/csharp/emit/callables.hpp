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
        : _symbol{std::move(sym)}, _wrapper_name{std::move(wrapper_name)},
          _delegate_expr{std::move(delegate_expr)}, _has_self{false} {}

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
        : _symbol{std::move(sym)}, _wrapper_name{std::move(wrapper_name)},
          _delegate_expr{std::move(delegate_expr)}, _self_handle_type{std::move(self_cs)},
          _self_handle_field{std::move(self_field)}, _has_self{true} {}

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
        std::string shim_params{_params.shim_params};
        if (_has_self)
            shim_params = "void* self" +
                          (_params.shim_params.empty() ? "" : ", " + _params.shim_params);
        shim_params += (shim_params.empty() ? "" : ", ");
        shim_params += "welder_error* err";
        std::string delegate_args{_has_self ? "self, err" : "err"};
        if (!_params.delegate_args.empty())
            delegate_args += ", " + _params.delegate_args;
        code_writer t{_symbol.thunk()};
        t.line("{} {}({}) { return {}({}); }", wire_return_v<R>, _symbol.name(),
               shim_params, _delegate_expr, delegate_args);
        t.blank();
    }

    /** Write the `[LibraryImport]` declaration (UTF-8 marshalling when any
        string crosses; `[return: MarshalAs(U1)]` for a `bool` return). */
    void emit_pinvoke() {
        std::string pin_params{_params.pinvoke_params};
        if (_has_self)
            pin_params = _self_handle_type + " self" +
                         (_params.pinvoke_params.empty() ? ""
                                                     : ", " + _params.pinvoke_params);
        pin_params += (pin_params.empty() ? "" : ", ");
        pin_params += "out WelderError err";
        constexpr bool r_is_bool{classify(R) == marshal_kind::boolean};
        _symbol.pinvoke().line(
            "{} {}internal static partial {} {}({});",
            import_attr(_params.has_string),
            r_is_bool ? "[return: MarshalAs(UnmanagedType.U1)] " : "",
            pinvoke_type<R, Style>(true), _symbol.name(), pin_params);
    }

    /** Write the managed wrapper: XML docs, the signature, and the return
        path (@ref wrapper_return_body) inside whatever staging the parameter
        list demands (@ref call_pieces::wrap). */
    void emit_wrapper() {
        code_writer w{_symbol.wrapper()};
        std::string call_args{_has_self ? _self_handle_field : std::string{}};
        if (!_params.wrapper_args.empty())
            call_args += (call_args.empty() ? "" : ", ") + _params.wrapper_args;
        call_args += (call_args.empty() ? "" : ", ");
        call_args += "out WelderError _e";
        const std::string pc{"NativeMethods." + _symbol.name() + "(" + call_args +
                             ")"};
        std::string docs{};
        emit_callable_docs<Fn>(docs, w.indentation(), _params);
        w.raw(docs);
        constexpr bool ret_unsafe{classify(R) == marshal_kind::seq_value ||
                                  classify(R) == marshal_kind::tuple_value};
        w.line("public {}{}{} {}({})",
               _params.needs_unsafe || ret_unsafe ? "unsafe " : "",
               _has_self ? "" : "static ", public_return_type<R, Style>(),
               _wrapper_name, _params.wrapper_params);
        {
            const auto body{w.braces()};
            w.raw(_params.wrap(
                wrapper_return_body<R, Style,
                                    ::welder::return_policy_of(Fn, lang::cs)>(
                    pc, w.indentation() + (_params.post.empty() ? "" : "    "),
                    _has_self ? "this" : ""),
                w.indentation()));
        }
        w.blank();
    }

    bound_symbol _symbol;      /**< The symbol and its three coordinated sinks. */
    std::string _wrapper_name;      /**< The wrapper's resolved C# name. */
    std::string _delegate_expr;      /**< The thunk's delegate expression. */
    std::string _self_handle_type;   /**< Instance form: the handle class's C# type. */
    std::string _self_handle_field;/**< Instance form: the handle-field expression. */
    bool _has_self;         /**< Whether this is an instance emission. */
    /** The parameter list in its five spellings (built once, shared by all
        three fragments). */
    call_pieces _params{build_params<Fn, Style>(
        std::make_index_sequence<std::meta::parameters_of(Fn).size()>{})};
};

} // namespace welder::inline v0::rods::csharp
