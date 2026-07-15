#pragma once
#include <cstddef>
#include <meta>
#include <string>

#include <welder/rods/python/trampoline.hpp> // overridable_virtuals / bound_flat

/** @file
    The text-emitting core of welder's **trampoline-generator** rod: the consteval
    routine that renders a hand-written-equivalent pybind11/nanobind trampoline for a
    welded virtual type, plus the small `document` that accumulates the generated
    structs and their `trampoline_for` registrations.

    welder cannot *synthesize* a trampoline as a live C++ class (member-declaration
    injection is absent from P2996), but it can **emit one as source text** and let the
    consuming translation unit compile it — the same build-time text-emission model as
    the LuaCATS stub rod. The one hard part, reproducing each virtual's signature
    exactly, is sidestepped: the generated header is itself compiled with reflection, so
    every return / parameter type is a **splice** of the base virtual's own reflected
    type (`[: std::meta::type_of(…) :]`) rather than a respelled type name — so the
    override matches the base signature by construction, for arbitrarily hairy types.
    The cv / ref / `noexcept` qualifiers are emitted from the matching reflection
    queries. Slots come from @ref welder::rods::python::overridable_virtuals, so the
    generated trampoline covers inherited virtuals and honours `bind_flat`.

    The one signature shape reflection cannot reproduce is a **C-style variadic**
    virtual (`f(…, ...)`): P2996 exposes no ellipsis query. Such a virtual, unless it is
    marked `bind_flat`, makes the generator emit a `static_assert` so the omission is a
    clear compile error rather than a mysterious "marked override, does not override".

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`). */

namespace welder::inline v0::rods::trampolines {

// --- small consteval text helpers -------------------------------------------

/** Render a non-negative integer as decimal (constexpr `std::to_string` is not
    available on gcc-16 in a constant-evaluated context). */
consteval std::string int_string(std::size_t n) {
    if (n == 0)
        return "0";
    std::string s{};
    for (; n; n /= 10)
        s.insert(s.begin(), char('0' + n % 10));
    return s;
}

/** The fully `::`-qualified name of a namespace-scope entity, leading `::` included
    (`geometry::Point` → `"::geometry::Point"`). Used both to name a trampoline's base
    and to splice the type in the generated file from any namespace. */
consteval std::string cpp_qualified_name(std::meta::info ent) {
    std::string tail{};
    if (std::meta::has_identifier(ent))
        tail = std::meta::identifier_of(ent);
    for (std::meta::info p{std::meta::parent_of(ent)};
         p != std::meta::info{} && p != ^^:: && std::meta::has_identifier(p);
         p = std::meta::parent_of(p))
        tail = std::string{std::meta::identifier_of(p)} + "::" + tail;
    return "::" + tail;
}

/** A collision-free identifier for @a type's generated trampoline: its qualified name
    with `::` folded to `_` (`geometry::Point` → `geometry_Point_trampoline`). */
consteval std::string trampoline_ident(std::meta::info type) {
    std::string s{};
    if (std::meta::has_identifier(type))
        s = std::meta::identifier_of(type);
    for (std::meta::info p{std::meta::parent_of(type)};
         p != std::meta::info{} && p != ^^:: && std::meta::has_identifier(p);
         p = std::meta::parent_of(p))
        s = std::string{std::meta::identifier_of(p)} + "_" + s;
    return s + "_trampoline";
}

/** Is @a fn a C-style variadic function (`R(…, ...)`)?

    C++26 reflection has no ellipsis/variadic predicate, so this reads the ellipsis
    back out of the implementation's type spelling — acceptable because it only gates a
    build-time *error path* (a variadic virtual with no `bind_flat`), never generated
    code. A concrete function type's display never contains `...` for any other reason
    (all its parameter types are already instantiated). */
consteval bool is_c_variadic(std::meta::info fn) {
    std::string s{std::meta::display_string_of(std::meta::type_of(fn))};
    return s.find("...") != std::string::npos;
}

/** The trailing cv / ref-qualifier / `noexcept` tokens of a member function @a fn, in
    grammatical order (`" const & noexcept"`), each with a leading space. */
consteval std::string qualifier_tokens(std::meta::info fn) {
    std::string q{};
    if (std::meta::is_const(fn))
        q += " const";
    if (std::meta::is_volatile(fn))
        q += " volatile";
    if (std::meta::is_lvalue_reference_qualified(fn))
        q += " &";
    if (std::meta::is_rvalue_reference_qualified(fn))
        q += " &&";
    if (std::meta::is_noexcept(fn))
        q += " noexcept";
    return q;
}

/** The namespace the generated trampoline structs are emitted into (fully qualified). */
inline constexpr const char* generated_namespace{
    "::welder::rods::trampolines::generated"};

/** Render the trampoline `struct` for welded virtual type @a type — one override per
    overridable virtual, each splicing the base's own reflected return/parameter types
    and forwarding to Python via `WELDER_PY_OVERRIDE_AS` (the slot-reflection form, so
    overloaded virtuals dispatch correctly).

    @param type a reflection of the welded virtual type.
    @return the `struct <ident> : <Base> { … };` text (no surrounding namespace). */
consteval std::string render_trampoline(std::meta::info type) {
    const std::string base{cpp_qualified_name(type)};
    const std::string ident{trampoline_ident(type)};
    std::string s{};
    s += "struct " + ident + " : " + base + " {\n";
    s += "    WELDER_PY_TRAMPOLINE(" + base + ");\n";

    std::size_t k{0};
    for (auto slot : ::welder::rods::python::overridable_virtuals(type)) {
        const std::string name{std::meta::identifier_of(slot)};
        if (is_c_variadic(slot)) {
            s += "    static_assert(false, \"welder: '" + base + "::" + name +
                 "' is a C-variadic virtual; C++26 reflection cannot reproduce its "
                 "'...' parameter. Mark it "
                 "[[=welder::rods::python::bind_flat]] to bind it non-overridably.\");\n";
            ++k; // keep k aligned with the overridable_virtuals index for later slots
            continue;
        }
        // The reflected slot, re-derived in the generated TU (same order as here).
        const std::string idx{"::welder::rods::python::overridable_virtuals(^^" + base +
                              ")[" + int_string(k) + "]"};
        s += "    [: ::std::meta::return_type_of(" + idx + ") :] " + name + "(";
        std::string args{};
        std::size_t j{0};
        for (auto p : std::meta::parameters_of(slot)) {
            (void)p;
            if (j) {
                s += ", ";
                args += ", ";
            }
            const std::string js{int_string(j)};
            s += "[: ::std::meta::type_of(::std::meta::parameters_of(" + idx + ")[" +
                 js + "]) :] a" + js;
            args += "a" + js;
            ++j;
        }
        // The slot-taking macro form: dispatch keys on the slot's reflection (never
        // `^^Base::name`, which is ill-formed for an overloaded virtual), while the
        // textual name spells the qualified base fallback, where overload resolution
        // picks the right overload from the forwarded parameters.
        s += ")" + qualifier_tokens(slot) + " override { WELDER_PY_OVERRIDE_AS((" +
             idx + "), " + name + (args.empty() ? "" : ", " + args) + "); }\n";
        ++k;
    }
    s += "};\n";
    return s;
}

/** The `trampoline_for<type>` specialization pointing at @a type's generated struct. */
consteval std::string render_registration(std::meta::info type) {
    return "template <> constexpr ::std::meta::info\n"
           "    ::welder::rods::python::trampoline_for<" +
           cpp_qualified_name(type) + "> = ^^" + generated_namespace +
           "::" + trampoline_ident(type) + ";\n";
}

/** The growing generated header: the trampoline `struct` bodies and their
    `trampoline_for` registrations, kept apart so the structs can share one
    `namespace generated { … }` block and the registrations sit at global scope. */
struct document {
    std::string structs{};        /**< Accumulated trampoline `struct` definitions. */
    std::string registrations{};  /**< Accumulated `trampoline_for` specializations. */

    /** Append the generated trampoline + registration for welded virtual @a Type.

        A template rather than a runtime parameter: `std::meta::info` is a
        consteval-only type, so the reflection must arrive as a non-type template
        argument (which is exactly what the rod's `make_class<T>` has via `^^T`). The
        rendered text is materialized to a `const char*` and appended at runtime. */
    template <std::meta::info Type>
    void add() {
        structs += std::define_static_string(render_trampoline(Type));
        registrations += std::define_static_string(render_registration(Type));
    }

    /** The finished, self-contained header text. */
    std::string render() const {
        std::string out{};
        out += "#pragma once\n";
        out += "// AUTO-GENERATED by welder (welder::rods::trampolines). Do not edit.\n";
        out += "//\n";
        out += "// Include AFTER your welded type headers and the active backend's\n";
        out += "// <welder/rods/python/{pybind11,nanobind}/trampoline.hpp>.\n";
        out += "#include <meta>\n";
        out += "#include <welder/rods/python/trampoline.hpp>\n\n";
        out += "namespace welder::rods::trampolines::generated {\n\n";
        out += structs;
        out += "\n} // namespace welder::rods::trampolines::generated\n\n";
        out += registrations;
        return out;
    }
};

} // namespace welder::rods::trampolines