#pragma once
// Name styling + weld_as — mirrors tests/python/test_naming.py and
// tests/lua/spec/naming_spec.lua. Unlike the other shared groups, this one is bound
// through a *styled* welder: WELDER_TEST_STYLED_WELDER is `welder::welder<Rod,
// Style>` with the backend's chosen style (PEP 8 for Python, snake_case for Lua),
// defined by each bindings.cpp beside WELDER_TEST_WELDER. So the same C++ names come
// out reshaped per language, and each spec asserts its own convention.
//
// The cases live in namespace `styling`, bound under a `styling` submodule. Every
// member is authored camelCase/PascalCase so the styling is observable, and one
// method carries a per-language `weld_as` to prove the verbatim override wins over
// (and bypasses) the style.
//
// #included by bindings.cpp after the welder vocabulary + the active backend + its
// style header.

namespace styling {

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
HttpClient {
    // camelCase data member: PEP 8 / snake_case -> base_url.
    std::string baseUrl{};

    HttpClient() = default;
    HttpClient(std::string url) : baseUrl{std::move(url)} {}

    // camelCase methods -> send_request / retry_count.
    std::string sendRequest() const { return baseUrl + "/go"; }
    int retryCount() const { return 3; }

    // weld_as is ultimate: it does NOT flow through the style and is used verbatim,
    // and it is per-language. A snake_case/PEP 8 style would never emit uppercase,
    // so `PING` (py) / `ping` (lua) can only come from the override — while the
    // styled spelling `do_ping` must be absent in both.
    [[=welder::weld_as(welder::lang::py, "PING"),
      =welder::weld_as(welder::lang::lua, "ping")]]
    std::string doPing() const { return "pong"; }

    // static method -> default_port.
    static int defaultPort() { return 8080; }
};

} // namespace styling

inline void register_naming(WELDER_TEST_MODULE_T& m) {
    auto sub{WELDER_TEST_SUBMODULE(m, "styling")};
    WELDER_TEST_STYLED_WELDER::weld_namespace<^^styling>(sub);
}