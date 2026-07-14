// Name-styling + weld_as semantics (compile-only; must SUCCEED to compile).
//
// welder::naming reshapes a C++ identifier into a target-language convention,
// however the identifier was spelled (snake/camel/Pascal/SCREAMING), and
// welder::name_of resolves the final bound name — a [[=welder::weld_as]] override
// wins verbatim, else the entity's per-kind style hook reshapes the identifier.
// Both are consteval (their results feed define_static_string in the driver/rods),
// so the cases are locked as static_asserts. Same compile-to-pass shape as the
// other core tests; needs only the compiler.
#include <string_view>

#include <welder/vocabulary.hpp> // annotation vocabulary (weld / weld_as)
#include <welder/naming.hpp>     // split_words / restyle / name_of / weld_as_of

using namespace std::string_view_literals;
namespace nm = welder::naming;
using welder::ent_kind;
using welder::lang;

// --- word splitting: the same identifier, any source spelling, one word list ---
consteval bool words_are(std::string_view id,
                         std::initializer_list<std::string_view> expect) {
    auto ws{nm::split_words(id)};
    if (ws.size() != expect.size())
        return false;
    std::size_t i{0};
    for (auto e : expect)
        if (ws[i++] != e)
            return false;
    return true;
}
static_assert(words_are("processFile"sv, {"process"sv, "file"sv}));    // camelCase
static_assert(words_are("ProcessFile"sv, {"process"sv, "file"sv}));    // PascalCase
static_assert(words_are("process_file"sv, {"process"sv, "file"sv}));   // snake_case
static_assert(words_are("PROCESS_FILE"sv, {"process"sv, "file"sv}));   // SCREAMING
static_assert(words_are("HTTPServer"sv, {"http"sv, "server"sv}));      // acronym run
static_assert(words_are("vec3Length"sv, {"vec3"sv, "length"sv}));      // digits stay put

// --- restyle: split, then re-join in the target convention -------------------
consteval bool restyled(std::string_view id, nm::case_kind k, std::string_view e) {
    return nm::restyle(id, k) == e;
}
static_assert(restyled("processFile"sv, nm::case_kind::snake, "process_file"sv));
static_assert(restyled("process_file"sv, nm::case_kind::pascal, "ProcessFile"sv));
static_assert(restyled("HTTPServer"sv, nm::case_kind::camel, "httpServer"sv));
static_assert(restyled("max_retries"sv, nm::case_kind::screaming_snake, "MAX_RETRIES"sv));
static_assert(restyled("foo_bar"sv, nm::case_kind::kebab, "foo-bar"sv));
// leading/trailing fixup underscores survive; an all-underscore token is untouched.
static_assert(restyled("_privateThing"sv, nm::case_kind::snake, "_private_thing"sv));
static_assert(restyled("type_"sv, nm::case_kind::pascal, "Type_"sv));

// --- a demo type carrying weld_as overrides + varied spellings ---------------
namespace demo {
struct [[=welder::weld(welder::lang::py, welder::lang::lua)]] FileProcessor {
    int firstValue;

    // A per-language verbatim rename: `process` in Python, `Process` in Lua.
    [[=welder::weld_as(welder::lang::py, "process")]]
    [[=welder::weld_as(welder::lang::lua, "Process")]]
    void processPython() {}

    // A no-language weld_as covers every language verbatim.
    [[=welder::weld_as("do_it")]]
    void doItYourself() {}

    // Several language markers before the name: one verbatim name for py AND lua
    // (but not any other language), in a single annotation.
    [[=welder::weld_as(welder::lang::py, welder::lang::lua, "flush_all")]]
    void flushEverything() {}

    void computeChecksum() {}
};
} // namespace demo

consteval bool name_is(std::string_view got, std::string_view expect) {
    return got == expect;
}

// weld_as wins verbatim and is language-scoped; it never flows through the style.
static_assert(name_is(
    welder::name_of<^^demo::FileProcessor::processPython, lang::py, nm::snake_case,
                    ent_kind::method>(),
    "process"sv));
static_assert(name_is(
    welder::name_of<^^demo::FileProcessor::processPython, lang::lua, nm::snake_case,
                    ent_kind::method>(),
    "Process"sv));
// A bare weld_as applies to every language.
static_assert(name_is(
    welder::name_of<^^demo::FileProcessor::doItYourself, lang::py, nm::snake_case,
                    ent_kind::method>(),
    "do_it"sv));
// A multi-language weld_as (several markers, one name) covers each listed language.
static_assert(name_is(
    welder::name_of<^^demo::FileProcessor::flushEverything, lang::py, nm::snake_case,
                    ent_kind::method>(),
    "flush_all"sv));
static_assert(name_is(
    welder::name_of<^^demo::FileProcessor::flushEverything, lang::lua, nm::snake_case,
                    ent_kind::method>(),
    "flush_all"sv));
static_assert(
    std::string_view{
        welder::weld_as_of<^^demo::FileProcessor::flushEverything, lang::lua>()} ==
    "flush_all"sv);

// No weld_as: the per-kind style hook reshapes the identifier.
static_assert(name_is(
    welder::name_of<^^demo::FileProcessor::computeChecksum, lang::py, nm::snake_case,
                    ent_kind::method>(),
    "compute_checksum"sv));
static_assert(name_is(
    welder::name_of<^^demo::FileProcessor::firstValue, lang::py, nm::snake_case,
                    ent_kind::field>(),
    "first_value"sv));

// The default style (none) is the identity — verbatim identifier, no weld_as here.
static_assert(name_is(
    welder::name_of<^^demo::FileProcessor::computeChecksum, lang::py, nm::none,
                    ent_kind::method>(),
    "computeChecksum"sv));

// weld_as_of reports the override directly (nullptr when absent / wrong language).
static_assert(welder::weld_as_of<^^demo::FileProcessor::computeChecksum, lang::py>() ==
              nullptr);
static_assert(
    std::string_view{
        welder::weld_as_of<^^demo::FileProcessor::processPython, lang::py>()} ==
    "process"sv);

// --- name_of_or: the call-site override form ---------------------------------
// The override wins verbatim (beating weld_as); nullptr falls back to name_of.
static_assert(name_is(
    welder::name_of_or<^^demo::FileProcessor::processPython, lang::py, nm::snake_case,
                       ent_kind::method>("forced"),
    "forced"sv));
static_assert(name_is(
    welder::name_of_or<^^demo::FileProcessor::computeChecksum, lang::py,
                       nm::snake_case, ent_kind::method>(nullptr),
    "compute_checksum"sv));

// The fallback is LAZY: an entity with no identifier (a template instantiation)
// still resolves when the override is supplied — the whole reason weld_type<
// Box<int>>(m, "IntBox") is legitimate. (Without identifier, weld_as or override,
// name_of_or throws at binding time; that path is runtime-only by design.)
namespace demo {
template <class T>
struct Bucket {
    T v;
};
} // namespace demo
static_assert(name_is(
    welder::name_of_or<^^demo::Bucket<int>, lang::py, nm::none, ent_kind::class_>(
        "IntBucket"),
    "IntBucket"sv));
static_assert(!std::meta::has_identifier(^^demo::Bucket<int>)); // why it must be lazy

int main() {}
