#pragma once
#include <welder/detail/config.hpp>
#include <welder/lang.hpp>

// The annotation vocabulary users attach to their types. Like <welder/lang.hpp>,
// this is std-include-free so it can be exported by the `welder` module.

WELDER_EXPORT namespace welder {

// Languages are tracked as bits in a mask so a spec can name an arbitrary
// subset. For exclude/include specs a mask of 0 is the sentinel meaning
// "all welded languages".
consteval unsigned lang_bit(lang l) {
    return 1u << static_cast<unsigned>(l);
}

template <class... Ls>
consteval unsigned lang_mask(Ls... ls) {
    return (0u | ... | lang_bit(ls));
}

// --- weld: the type-level annotation declaring target languages -------------

struct weld_spec {
    unsigned mask = 0;
};

// Usage: [[=welder::weld(welder::lang::py, welder::lang::lua)]]
template <class... Ls>
consteval weld_spec weld(Ls... ls) {
    return weld_spec{lang_mask(ls...)};
}

// --- policy: how greedily members are reflected -----------------------------

enum class policy_kind : unsigned char {
    automatic, // reflect every member unless explicitly excluded (default)
    opt_in,    // reflect only members explicitly marked include
};

struct policy_spec {
    policy_kind kind = policy_kind::automatic;
};

namespace policy {
// Usage: [[=welder::policy::automatic]] (`auto` is reserved, hence the spelling)
inline constexpr policy_spec automatic{policy_kind::automatic};
inline constexpr policy_spec opt_in{policy_kind::opt_in};
} // namespace policy

// --- mark: member-level include/exclude -------------------------------------
//
// Each marker is a constexpr object usable bare as an annotation (applies to all
// languages) or called with languages to scope it:
//   [[=welder::mark::exclude]]                    -> all languages
//   [[=welder::mark::exclude(welder::lang::lua)]] -> lua only

struct exclude_spec {
    unsigned mask = 0; // 0 == all languages

    template <class... Ls>
    consteval exclude_spec operator()(Ls... ls) const {
        return exclude_spec{lang_mask(ls...)};
    }
};

struct include_spec {
    unsigned mask = 0; // 0 == all languages

    template <class... Ls>
    consteval include_spec operator()(Ls... ls) const {
        return include_spec{lang_mask(ls...)};
    }
};

namespace mark {
inline constexpr exclude_spec exclude{};
inline constexpr include_spec include{};
} // namespace mark

} // namespace welder
