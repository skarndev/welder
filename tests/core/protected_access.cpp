// Protected-member access admission (compile-only): the core semantics behind
// policy::weld_protected. The runtime behavior is covered by test_resolution.py /
// resolution_spec.lua (annotation, lang scoping, opt_in composition), the
// templates cases (annotation read through an instantiation), the
// gen_trampolines cases (trampolined NVI hook) and test_namespace.py's tack knob.
// These static_asserts lock the predicates:
//
//   - protected_welded: bare = all languages; scoped masks; repeats union;
//     read through a class-template instantiation from the template;
//   - member_access_admitted: public always in; PROTECTED arbitrated by the
//     resolution's optional protected_participates(mem, L, bound_into) hook,
//     falling back to the annotation; the hook can key on bound_into (the
//     entity whose binding receives the member — the flattening target);
//     PRIVATE hard-out before the hook — even a hook answering `true` for
//     everything cannot readmit a private member. Exposing private is a
//     violation of welder's design, not a policy a resolution may choose.
//     (The carriage-side threading of bound_into — ^^T held through the
//     base-flattening recursion — is locked at runtime by the foreign_mixed
//     tack case in namespace.hpp ↔ test_namespace.py / namespace_spec.lua.)
#include <welder/vocabulary.hpp>
#include <welder/bind_traits.hpp>

#include <string_view>

namespace pa {

struct [[=welder::weld(welder::lang::py)]] Open { // no weld_protected
    int pub{};

  protected:
    int prot{};
    int prot_fn() const { return prot; }

  private:
    int priv{};
};

struct
[[=welder::weld(welder::lang::py)]]
[[=welder::policy::weld_protected]]
Bare { // bare form: all languages
  protected:
    int prot{};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
[[=welder::policy::weld_protected(welder::lang::py)]]
Scoped { // py only
  protected:
    int prot{};
};

struct
[[
  =welder::weld(welder::lang::py, welder::lang::lua),
  =welder::policy::weld_protected(welder::lang::py),
  =welder::policy::weld_protected(welder::lang::lua)
]]
Repeated { // repeats union
  protected:
    int prot{};
};

template <class T>
struct
[[=welder::weld(welder::lang::py)]]
[[=welder::policy::weld_protected]]
Tpl { // read through the instantiation
  protected:
    T prot{};
};
using IntTpl = Tpl<int>;

// --- protected_welded: the annotation reader ---------------------------------

static_assert(!welder::protected_welded(^^Open, welder::lang::py));
static_assert(welder::protected_welded(^^Bare, welder::lang::py));
static_assert(welder::protected_welded(^^Bare, welder::lang::lua)); // bare = all
static_assert(welder::protected_welded(^^Scoped, welder::lang::py));
static_assert(!welder::protected_welded(^^Scoped, welder::lang::lua));
static_assert(welder::protected_welded(^^Repeated, welder::lang::py));
static_assert(welder::protected_welded(^^Repeated, welder::lang::lua));
// Read through the INSTANTIATION (what parent_of(mem) always hands the reader);
// the bare alias reflection carries no annotations of its own.
static_assert(welder::protected_welded(std::meta::dealias(^^IntTpl),
                                       welder::lang::py));
static_assert(!welder::protected_welded(^^IntTpl, welder::lang::py));

// --- member_access_admitted: the carriage's composition -----------------------

// find a nonstatic data member / member function by name, unchecked context
consteval std::meta::info member_named(std::meta::info cls, std::string_view n) {
    for (auto m : std::meta::members_of(
             cls, std::meta::access_context::unchecked()))
        if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == n)
            return m;
    return std::meta::info{};
}

// a hookless resolution: falls back to the annotation
struct hookless {};

// the shipped default, spelled out (bound_into forwarded, unused)
struct annotation_driven {
    static consteval bool protected_participates(std::meta::info mem,
                                                 welder::lang L,
                                                 std::meta::info) {
        return welder::protected_welded(std::meta::parent_of(mem), L);
    }
};

// a blanket hook, like greedy_resolution<true> — and the probe that private
// stays out even under it
struct admit_everything {
    static consteval bool protected_participates(std::meta::info, welder::lang,
                                                 std::meta::info) {
        return true;
    }
};

// a closing hook: a resolution may also *refuse* protected wholesale
struct admit_nothing {
    static consteval bool protected_participates(std::meta::info, welder::lang,
                                                 std::meta::info) {
        return false;
    }
};

// a bound_into-keyed hook: admit a (possibly unannotated) declaring class's
// protected members only when they land on ONE specific binding — the
// flattening-target scoping the trailing parameter exists for
struct admit_into_bare_only {
    static consteval bool protected_participates(std::meta::info, welder::lang,
                                                 std::meta::info bound_into) {
        return bound_into == ^^Bare;
    }
};

namespace d = welder::detail;
constexpr auto py{welder::lang::py};
constexpr auto lua{welder::lang::lua};

// bound_into for direct (non-flattened) members is the declaring class itself;
// the shorthand keeps the asserts readable
consteval bool admitted_hookless(std::meta::info cls, std::string_view n,
                                 welder::lang L) {
    return d::member_access_admitted<hookless>(member_named(cls, n), L, cls);
}

// public: always admitted, under any resolution
static_assert(admitted_hookless(^^Open, "pub", py));
static_assert(d::member_access_admitted<admit_nothing>(member_named(^^Open, "pub"),
                                                       py, ^^Open));

// protected, hookless resolution: the annotation decides
static_assert(!admitted_hookless(^^Open, "prot", py));
static_assert(!admitted_hookless(^^Open, "prot_fn", py));
static_assert(admitted_hookless(^^Bare, "prot", py));
static_assert(admitted_hookless(^^Scoped, "prot", py));
static_assert(!admitted_hookless(^^Scoped, "prot", lua));
static_assert(admitted_hookless(^^IntTpl, "prot", py));

// protected, with a hook: the hook REPLACES the annotation default
static_assert(d::member_access_admitted<annotation_driven>(
    member_named(^^Bare, "prot"), py, ^^Bare));
static_assert(d::member_access_admitted<admit_everything>(
    member_named(^^Open, "prot"), py, ^^Open));
static_assert(!d::member_access_admitted<admit_nothing>(
    member_named(^^Bare, "prot"), py, ^^Bare));

// the hook can key on bound_into: the SAME member is admitted into one binding
// and refused from another (the flattening-target distinction)
static_assert(d::member_access_admitted<admit_into_bare_only>(
    member_named(^^Open, "prot"), py, ^^Bare));
static_assert(!d::member_access_admitted<admit_into_bare_only>(
    member_named(^^Open, "prot"), py, ^^Open));

// PRIVATE: hard-out before the hook — admit_everything cannot readmit it
static_assert(!admitted_hookless(^^Open, "priv", py));
static_assert(!d::member_access_admitted<admit_everything>(
    member_named(^^Open, "priv"), py, ^^Open));

// --- the shape predicates: private rejected in the shape ----------------------

static_assert(d::is_method_candidate(member_named(^^Open, "prot_fn")));
static_assert(!d::is_method_candidate(member_named(^^Bare, "prot"))); // data, not a method

struct Shapes {
    void pub_fn() {}

  protected:
    void prot_fn() {}

  private:
    void priv_fn() {}
};
static_assert(d::is_method_candidate(member_named(^^Shapes, "pub_fn")));
static_assert(d::is_method_candidate(member_named(^^Shapes, "prot_fn")));
static_assert(!d::is_method_candidate(member_named(^^Shapes, "priv_fn")));

} // namespace pa