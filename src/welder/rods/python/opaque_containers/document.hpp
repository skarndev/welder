#pragma once
#include <algorithm>
#include <cstddef>
#include <meta>
#include <string>
#include <vector>

#include <welder/containers.hpp> // is_reference_container / container_kind_of
#include <welder/naming.hpp>     // restyle / case_kind (name derivation)

/** @file
    The text-emitting core of welder's **opaque-container generator** rod: the
    collector that gathers the reference containers a welded namespace uses, derives a
    readable target name for each, and renders the `.hpp` of `WELDER_OPAQUE(...)`
    declarations + welded aliases that binds them by reference.

    welder cannot emit a namespace-scope `type_caster` specialization
    (`PYBIND11_MAKE_OPAQUE`/`NB_MAKE_OPAQUE`) from a *runtime* rod — the same wall the
    trampoline generator hits with subclass injection — so this build-time text rod
    emits it as source the consuming translation unit compiles, exactly like the
    trampoline and LuaCATS generators. It is **backend-neutral**: it emits the neutral
    `WELDER_OPAQUE` macro + `weld(lang::py)` aliases, so one generated header serves
    both Python rods.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`). */

namespace welder::inline v0::rods::opaque_containers {

// --- type spelling & name derivation (consteval) ----------------------------

/** The C++ text of container @a type, infrastructure args (allocator/comparator/
    hasher) dropped — the reflection *display* form, which already elides them
    (`std::vector<int>`, `std::map<std::__cxx11::basic_string<char>, int>`). Valid,
    include-order-robust C++ for a `WELDER_OPAQUE(...)` / alias right-hand side. */
consteval std::string container_spelling(std::meta::info type) {
    return std::string{std::meta::display_string_of(std::meta::dealias(type))};
}

/** How many leading (value) template arguments of @a type name its element/key/value
    types — a sequence exposes 1, a map 2. @pre @a type is a reference container. */
consteval std::size_t value_arg_count(std::meta::info type) {
    return ::welder::container_kind_of(type) == ::welder::container_kind::map ? 2u : 1u;
}

/** A readable PascalCase identifier for container @a type: the template name plus
    each value argument's name, recursing — `vector<int>` → `VectorInt`,
    `map<string,int>` → `MapStringInt`, `unordered_map<int,double>` →
    `UnorderedMapIntDouble`, nested `map<string,vector<int>>` → `MapStringVectorInt`.
    `std::string` folds to `String`; a scalar / welded class uses its (PascalCased)
    identifier. Reuses `::welder::naming::restyle`. */
consteval std::string derive_name(std::meta::info type) {
    type = std::meta::dealias(type);
    // std::string (and wstring, …) is basic_string<Char, …> — fold to a clean word.
    if (std::meta::has_template_arguments(type) &&
        std::meta::template_of(type) == ^^std::basic_string)
        return "String";
    if (::welder::is_reference_container(type)) {
        std::string out{::welder::naming::restyle(
            std::string{std::meta::identifier_of(std::meta::template_of(type))},
            ::welder::naming::case_kind::pascal)};
        const auto args{std::meta::template_arguments_of(type)};
        for (std::size_t i{0}, n{value_arg_count(type)}; i < n; ++i)
            out += derive_name(args[i]);
        return out;
    }
    const std::string id{std::meta::has_identifier(type)
                             ? std::string{std::meta::identifier_of(type)}
                             : std::string{std::meta::display_string_of(type)}};
    return ::welder::naming::restyle(id, ::welder::naming::case_kind::pascal);
}

/** The fully-qualified spelling of namespace @a ns for a `namespace X { … }` header
    block (no leading `::` — `geometry::detail`, not `::geometry::detail`). */
consteval std::string namespace_name(std::meta::info ns) {
    std::string tail{std::meta::identifier_of(ns)};
    for (std::meta::info p{std::meta::parent_of(ns)};
         p != std::meta::info{} && p != ^^:: && std::meta::has_identifier(p);
         p = std::meta::parent_of(p))
        tail = std::string{std::meta::identifier_of(p)} + "::" + tail;
    return tail;
}

// --- collecting the reference containers within a surface type --------------

/** Is @a t a *scalar* leaf — a fundamental type or a `std::basic_string` — i.e. one
    that needs no class registration of its own? */
consteval bool scalar_leaf(std::meta::info t) {
    t = std::meta::dealias(t);
    if (std::meta::is_fundamental_type(t))
        return true;
    return std::meta::has_template_arguments(t) &&
           std::meta::template_of(t) == ^^std::basic_string;
}

/** Is container @a type eligible for the generator — are all its element/key/value
    types *scalar* (fundamental or string)?

    The generator opens **scalar-element** containers only. Two reasons: (1) they are
    the zero-copy case (`numpy` needs a scalar buffer), and (2) they are
    *ordering-safe* — a scalar-element container has no welded-class element, so the
    carriage's native-first pre-pass can register it ahead of every type that uses it
    without a def-time-ordering artifact (a welded-class-element container would have
    to interleave with its element class and its users, which generated aliases —
    always emitted last — cannot). A `std::vector<Widget>` / nested
    `std::map<K, std::vector<V>>` therefore stays by value; hand-write its alias to
    open it opaque. @pre @a type is a reference container. */
consteval bool opaque_eligible(std::meta::info type) {
    const auto args{std::meta::template_arguments_of(type)};
    for (std::size_t i{0}, n{value_arg_count(type)}; i < n; ++i)
        if (!scalar_leaf(args[i]))
            return false;
    return true;
}

/** Append @a type's directly-named reference container when it is one and eligible
    (see @ref opaque_eligible). cv/ref-stripped like the bindability gate; non-eligible
    (welded-class-element or nested) containers, and non-container wrappers
    (`optional`, `pair`), are left by value.
    @param out the accumulator. @param type a surface (member/param/return) type. */
consteval void collect_into(std::vector<std::meta::info>& out, std::meta::info type) {
    const std::meta::info u{std::meta::dealias(std::meta::remove_cvref(type))};
    if (::welder::is_reference_container(u) && opaque_eligible(u))
        out.push_back(u);
}

/** The reference containers within surface type @a Type (a splice-ready static list
    for a `template for`). */
template <std::meta::info Type>
consteval std::vector<std::meta::info> containers_in() {
    std::vector<std::meta::info> out{};
    collect_into(out, Type);
    return out;
}

// --- the growing generated header -------------------------------------------

/** One container to open opaque: its C++ spelling, its derived target name, and
    whether a `by_value` mark excluded it (kept in the set so a later use site can be
    reconciled, but skipped at render). */
struct entry {
    std::string spelling; /**< e.g. `std::vector<int>`. */
    std::string name;     /**< e.g. `VectorInt`. */
    bool excluded;        /**< a `by_value` mark opted this container type out. */
};

/** The accumulator threaded through the rod's emission hooks: the deduped set of
    reference containers the welded namespace uses, and the renderer for the finished
    header. */
struct document {
    std::vector<entry> entries{}; /**< Deduped by C++ spelling. */

    /** Record container @a C (deduped by spelling; @a excluded OR-merged across use
        sites — one `by_value` anywhere opts the whole type out, opaqueness being
        module-wide). Rendered text is materialized to `const char*` and stored. */
    template <std::meta::info C>
    void add_one(bool excluded) {
        const char* sp{std::define_static_string(container_spelling(C))};
        const char* nm{std::define_static_string(derive_name(C))};
        for (entry& e : entries)
            if (e.spelling == sp) {
                e.excluded = e.excluded || excluded;
                return;
            }
        entries.push_back({std::string{sp}, std::string{nm}, excluded});
    }

    /** Collect every reference container within surface type @a SurfaceType (a data
        member / parameter / return type), tagging them @a excluded. */
    template <std::meta::info SurfaceType>
    void collect([[maybe_unused]] bool excluded) {
        // Unused when SurfaceType names no eligible container (the common case — the
        // template for below is then empty); [[maybe_unused]] keeps -Wunused quiet.
        template for (constexpr auto c :
                      std::define_static_array(containers_in<SurfaceType>()))
            add_one<c>(excluded);
    }

    /** Collect from callable @a Fn's parameter types and (unless a constructor) its
        return type — never `by_value` (that mark lives on data members). */
    template <std::meta::info Fn>
    void collect_callable() {
        template for (constexpr auto p :
                      std::define_static_array(std::meta::parameters_of(Fn)))
            collect<std::meta::type_of(p)>(false);
        if constexpr (!std::meta::is_constructor(Fn)) {
            using R = [:std::meta::return_type_of(Fn):];
            if constexpr (!std::is_void_v<R>)
                collect<std::meta::return_type_of(Fn)>(false);
        }
    }

    /** The finished, self-contained header text — `WELDER_OPAQUE(...)` at global
        scope, the welded aliases inside `namespace @a ns`, entries sorted by name so
        the output is deterministic. Two distinct container types deriving the same
        name emit an `#error` (this generator version cannot rename them). */
    std::string render(const std::string& ns) const {
        std::vector<entry> es{entries};
        std::sort(es.begin(), es.end(),
                  [](const entry& a, const entry& b) { return a.name < b.name; });
        for (std::size_t i{1}; i < es.size(); ++i)
            if (!es[i].excluded && !es[i - 1].excluded &&
                es[i].name == es[i - 1].name &&
                es[i].spelling != es[i - 1].spelling)
                return "#error welder: two distinct opaque containers derive the "
                       "same name '" +
                       es[i].name + "' (" + es[i - 1].spelling + " vs " +
                       es[i].spelling +
                       "); this generator cannot rename them automatically.\n";
        std::string out{};
        out += "#pragma once\n";
        out += "// AUTO-GENERATED by welder (welder::rods::opaque_containers). Do not edit.\n";
        out += "//\n";
        out += "// Include AFTER your welded type headers and the active backend's\n";
        out += "// <welder/rods/python/{pybind11,nanobind}/rod.hpp>, before the module.\n";
        out += "#include <map>\n#include <string>\n#include <unordered_map>\n#include <vector>\n\n";
        for (const entry& e : es)
            if (!e.excluded)
                out += "WELDER_OPAQUE(" + e.spelling + ")\n";
        out += "\nnamespace " + ns + " {\n";
        for (const entry& e : es)
            if (!e.excluded)
                out += "using " + e.name +
                       " [[=welder::weld(welder::lang::py)]] = " + e.spelling + ";\n";
        out += "} // namespace " + ns + "\n";
        return out;
    }
};

} // namespace welder::inline v0::rods::opaque_containers
