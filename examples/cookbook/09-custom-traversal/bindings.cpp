// Cookbook 09 — custom traversal: a pruning tack weld.
//
// The traversal's *which-participates* policy is a RESOLUTION — a stateless
// struct of consteval predicates the carriage consults as it walks. The shipped
// resolutions are ordinary structs, so customizing one is plain inheritance:
// this recipe subclasses the greedy (tack) resolution to honor a third-party
// library's own privacy convention — underscore-prefixed names and `detail` /
// `impl` namespaces stay out — and plugs it back into basic_carriage as
// welder::welder's third template argument.
// docs/content/cookbook/custom-traversal.md walks through this file.
#include <string_view>

#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/rod.hpp>

#include "sensorlib.hpp" // the "third-party" header — zero welder annotations

// Tack welding, minus what the library's conventions mark private. Overriding a
// predicate = the override decides, delegating to the greedy base for the rest.
struct skip_private : welder::carriages::greedy_resolution {
    /** The library's privacy convention: `_name`, `detail`, `impl`. */
    static consteval bool hidden(std::meta::info entity) {
        if (!std::meta::has_identifier(entity))
            return false;
        const std::string_view name{std::meta::identifier_of(entity)};
        return name.starts_with('_') || name == "detail" || name == "impl";
    }

    /** Namespace-scope classes / functions / variables: skip the hidden ones. */
    static consteval bool member_participates(std::meta::info mem, welder::lang L,
                                              welder::policy_kind pol) {
        return !hidden(mem) &&
               welder::carriages::greedy_resolution::member_participates(mem, L, pol);
    }

    /** Nested namespaces: prune `detail` & friends wholesale (no recursion). */
    static consteval bool namespace_participates(std::meta::info ns, welder::lang L,
                                                 welder::policy_kind pol) {
        return !hidden(ns) &&
               welder::carriages::greedy_resolution::namespace_participates(ns, L,
                                                                            pol);
    }

    /** The legacy C-string API surface, detected from the SIGNATURE. */
    static consteval bool takes_c_string(std::meta::info fn) {
        for (auto p : std::meta::parameters_of(fn))
            if (std::meta::dealias(std::meta::type_of(p)) ==
                std::meta::dealias(^^const char*))
                return true;
        return false;
    }

    /** Class members — fields, methods, operators, constructors — resolve here,
        PER OVERLOAD: the carriage computes each name's overload group from this
        predicate, so the modern `label(string)` binds while the legacy
        `label(const char*, int)` sibling is pruned, with no annotation anywhere.
        The underscore rule applies inside classes too. */
    static consteval bool class_member_participates(std::meta::info mem,
                                                    welder::lang L,
                                                    welder::policy_kind pol) {
        if (hidden(mem))
            return false;
        if (std::meta::is_function(mem) && takes_c_string(mem))
            return false;
        return welder::carriages::greedy_resolution::class_member_participates(
            mem, L, pol);
    }

    /** Keep the bindability gate's registration oracle consistent with the skip
        rule: a type this resolution skips is never registered, so a public
        signature naming one must stay a compile-time error, not a call-time
        surprise. */
    static consteval bool counts_as_registered(std::meta::info type,
                                               welder::lang L) {
        return !hidden(type) &&
               welder::carriages::greedy_resolution::counts_as_registered(type, L);
    }
};

PYBIND11_MODULE(sensors, m) {
    m.doc() = "welder cookbook 09 - custom traversal (a pruning tack weld)";
    using pruned_tack =
        welder::welder<welder::rods::pybind11::rod<>, welder::naming::none,
                       welder::carriages::basic_carriage<skip_private>>;
    pruned_tack::weld_namespace<^^sensorlib>(m);
}