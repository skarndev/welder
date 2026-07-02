// Template ↔ annotation semantics (compile-only; must SUCCEED to compile).
//
// P2996 does not allow reading annotations off an *uninstantiated* template or
// concept — annotations_of throws there (it accepts only types / variables /
// functions / namespaces / enumerators / parameters). But annotations on a
// template DECLARATION are not lost: every *instantiation* carries the
// annotations of the declaration that governs it. This file locks those
// semantics in as static_asserts:
//
//   1. a primary template's annotation is readable through any instantiation;
//   2. an explicit specialization's own annotation wins for it;
//   3. a partial specialization's own annotation wins for what it governs;
//   4. member annotations inside a class-template instantiation resolve —
//      field docs, method doc/returns, parameter docs;
//   5. `weld` and marks on a class template resolve through instantiations
//      (welded_for / excluded_for) — so binding a concrete instantiation like
//      Welded<int> is legitimate. NB bind<T>'s *name default* is unusable there
//      (a specialization has no identifier — see 8); pass a name explicitly.
//   6. a substitute()d function-template instantiation carries summary /
//      returns / parameter docs;
//   7. likewise a variable-template instantiation;
//   8. naming: has_identifier(Box<int>) is false (identifier_of would throw);
//      the template's own identifier is reachable via template_of.
//
// The docs pipeline consequence: the walker cannot enumerate what does not
// exist (uninstantiated templates stay skipped), but any instantiation a user
// hands it — or that appears as a member/parameter type — has full docs.
#include <meta>

#include <welder/welder.hpp> // vocabulary + reflection (header-only core)

namespace probe {

// 1./4. primary with annotated members
template <class T>
struct [[=welder::doc("Primary Box.")]] Box {
    [[=welder::doc("the stored value")]] T v;
    [[=welder::doc("Get the value, scaled."), =welder::returns("v times k")]]
    T get([[=welder::doc("a scale factor")]] int k) const { return v * k; }
};

// 2. explicit specialization carrying its own annotation
template <>
struct [[=welder::doc("Box of int (explicit specialization).")]] Box<int> {
    int v;
};

// 3. partial specialization carrying its own annotation
template <class T>
struct [[=welder::doc("Box of pointer (partial specialization).")]] Box<T*> {
    T* v;
};

// 5. weld + marks on a class template
template <class T>
struct [[=welder::weld(welder::lang::py)]] Welded {
    T value;
    [[=welder::mark::exclude]] T hidden;
};

// 6. function template with summary / returns / a parameter doc
template <class T>
[[=welder::doc("Double a value."), =welder::returns("x plus x")]]
T twice([[=welder::doc("the value")]] T x) { return x + x; }

// 7. variable template
template <class T>
[[=welder::doc("The zero of T.")]] constexpr T zero{};

// 9. repeatable tparam docs riding on the template declaration
template <class K, class V>
struct [[=welder::doc("A dictionary."),
         =welder::tparam("K", "the key type"),
         =welder::tparam("V", "the mapped type")]] Dict {
    K key;
    V value;
};

} // namespace probe

namespace {

consteval bool streq(const char* a, const char* b) {
    if (a == nullptr || b == nullptr)
        return false;
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

constexpr auto ctx{std::meta::access_context::unchecked()};

// 1. primary's annotation through an instantiation
static_assert(streq(welder::doc_of<^^probe::Box<double>>(), "Primary Box."));
// 2. explicit specialization's own annotation
static_assert(streq(welder::doc_of<^^probe::Box<int>>(),
                    "Box of int (explicit specialization)."));
// 3. partial specialization's own annotation
static_assert(streq(welder::doc_of<^^probe::Box<char*>>(),
                    "Box of pointer (partial specialization)."));

// 4. members inside a class-template instantiation
static_assert(streq(
    welder::doc_of<std::meta::nonstatic_data_members_of(^^probe::Box<double>,
                                                        ctx)[0]>(),
    "the stored value"));
static_assert(streq(welder::doc_of<^^probe::Box<double>::get>(),
                    "Get the value, scaled."));
static_assert(streq(welder::return_doc_of<^^probe::Box<double>::get>(),
                    "v times k"));
constexpr auto get_params{welder::param_docs<^^probe::Box<double>::get>()};
static_assert(streq(get_params[0].name, "k"));
static_assert(streq(get_params[0].text, "a scale factor"));

// 5. weld + marks resolve through an instantiation
static_assert(welder::welded_for(^^probe::Welded<int>, welder::lang::py));
static_assert(welder::excluded_for(
    std::meta::nonstatic_data_members_of(^^probe::Welded<int>, ctx)[1],
    welder::lang::py));

// 6. substitute()d function-template instantiation
constexpr auto twice_int{std::meta::substitute(^^probe::twice, {^^int})};
static_assert(streq(welder::doc_of<twice_int>(), "Double a value."));
static_assert(streq(welder::return_doc_of<twice_int>(), "x plus x"));
constexpr auto twice_params{welder::param_docs<twice_int>()};
static_assert(streq(twice_params[0].name, "x"));
static_assert(streq(twice_params[0].text, "the value"));

// 7. variable-template instantiation
static_assert(streq(
    welder::doc_of<std::meta::substitute(^^probe::zero, {^^double})>(),
    "The zero of T."));

// 9. tparam docs (repeatable, ordered) read through an instantiation
constexpr auto dict_tparams{welder::tparam_docs<^^probe::Dict<int, double>>()};
static_assert(dict_tparams.size() == 2);
static_assert(streq(dict_tparams[0].name, "K"));
static_assert(streq(dict_tparams[0].text, "the key type"));
static_assert(streq(dict_tparams[1].name, "V"));
static_assert(streq(dict_tparams[1].text, "the mapped type"));
// an entity with no tparam annotations yields an empty array
static_assert(welder::tparam_docs<^^probe::Box<double>>().size() == 0);

// 8. a specialization has no identifier; the template's is one hop away
static_assert(!std::meta::has_identifier(^^probe::Box<int>));
static_assert(streq(std::define_static_string(std::meta::identifier_of(
                        std::meta::template_of(^^probe::Box<int>))),
                    "Box"));

} // namespace
