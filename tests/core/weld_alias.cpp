// Alias-welding semantics (compile-only): the reflect-layer rules behind welding a
// class-template instantiation through a namespace-scope alias. The runtime
// behavior is covered by tests/python/test_templates.py + tests/lua/spec/
// templates_spec.lua (all four runtime rods); the carriage diagnostics by the
// negcompile.alias_* cases. These static_asserts lock the predicates:
//
//   - names_template_specialization: alias -> class-template specialization only;
//   - alias_welded_for: the alias's own weld TAKES PRECEDENCE over the template's
//     (it does not union — an alias weld REPLACES the language set), and with no
//     alias weld the template's mark is read through the instantiation;
//   - alias_marks_admissible: weld / weld_as pass, every other welder mark is
//     inadmissible on an alias (it belongs on the template).
#include <welder/vocabulary.hpp>
#include <welder/reflect.hpp>

#include <string>

namespace wa {

template <class T>
struct [[=welder::weld(welder::lang::py)]] Welded {
    T v{};
};

template <class T>
struct Bare { // third-party-style: no weld of its own
    T v{};
};

struct [[=welder::weld(welder::lang::py)]] Plain {};

using WeldedInt = Welded<int>;                                        // template-welded
using BareInt [[=welder::weld(welder::lang::lua)]] = Bare<int>;       // alias-welded
using Narrowed [[=welder::weld(welder::lang::lua)]] = Welded<char>;   // alias REPLACES
using Renamed [[=welder::weld_as("Boxy")]] = Welded<long>;            // weld_as ok
using Documented [[=welder::doc("nope")]] = Welded<short>;            // inadmissible
using PlainAgain = Plain;                                             // not a specialization
using JustInt = int;                                                  // not a class

} // namespace wa

// --- names_template_specialization ---------------------------------------------
static_assert(welder::names_template_specialization(^^wa::WeldedInt));
static_assert(welder::names_template_specialization(^^wa::BareInt));
static_assert(!welder::names_template_specialization(^^wa::PlainAgain),
              "a plain-type alias is not a specialization vehicle");
static_assert(!welder::names_template_specialization(^^wa::JustInt));
static_assert(!welder::names_template_specialization(^^wa::Plain),
              "a non-alias type is not an alias at all");

// --- alias_welded_for: precedence, not union -------------------------------------
static_assert(welder::alias_welded_for(^^wa::WeldedInt, welder::lang::py),
              "no alias weld -> the template's mark is read through the instantiation");
static_assert(!welder::alias_welded_for(^^wa::WeldedInt, welder::lang::lua));
static_assert(welder::alias_welded_for(^^wa::BareInt, welder::lang::lua),
              "an unwelded template is opted in by the alias's own weld");
static_assert(!welder::alias_welded_for(^^wa::BareInt, welder::lang::py));
static_assert(welder::alias_welded_for(^^wa::Narrowed, welder::lang::lua) &&
                  !welder::alias_welded_for(^^wa::Narrowed, welder::lang::py),
              "an alias weld REPLACES the template's language set (precedence)");

// --- alias_marks_admissible ------------------------------------------------------
static_assert(welder::alias_marks_admissible(^^wa::WeldedInt)); // no marks at all
static_assert(welder::alias_marks_admissible(^^wa::BareInt));   // weld is allowed
static_assert(welder::alias_marks_admissible(^^wa::Renamed));   // weld_as is allowed
static_assert(!welder::alias_marks_admissible(^^wa::Documented),
              "doc (and every non-weld/weld_as mark) belongs on the template");