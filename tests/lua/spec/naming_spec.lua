-- Name styling + weld_as (mirrors tests/python/test_naming.py).
-- The `styling` submodule is bound through an all-snake_case styled welder, so the
-- camelCase/PascalCase C++ names come out snake_case — including the type itself
-- (HttpClient -> http_client) — except the method carrying a per-language `weld_as`,
-- which is forced verbatim (lua: `ping`) and bypasses the style.
local h = require("helper")
local m = h.mod.styling

describe("naming", function()
  it("snake_cases the type name", function()
    -- snake_case restyles every kind, so the class registers as http_client.
    assert.is_not_nil(m.http_client)                 -- HttpClient -> http_client
    assert.is_nil(rawget(m, "HttpClient"))           -- original spelling absent
  end)

  it("snake_cases methods and static methods", function()
    local c = m.http_client("http://x")
    assert.are.equal("http://x/go", c:send_request()) -- sendRequest -> send_request
    assert.are.equal(3, c:retry_count())              -- retryCount  -> retry_count
    assert.are.equal(8080, m.http_client.default_port()) -- static -> default_port
    h.assert_absent(c, "sendRequest")                 -- original spellings gone
    h.assert_absent(c, "retryCount")
  end)

  it("snake_cases data members", function()
    local c = m.http_client("http://x")
    assert.are.equal("http://x", c.base_url)          -- baseUrl -> base_url
    c.base_url = "http://y"
    assert.are.equal("http://y/go", c:send_request())
    h.assert_absent(c, "baseUrl")
  end)

  it("honours a per-language weld_as verbatim", function()
    local c = m.http_client()
    assert.are.equal("pong", c:ping())                -- lua override, verbatim
    h.assert_absent(c, "do_ping")                     -- styled spelling absent
    h.assert_absent(c, "doPing")                      -- original spelling absent
    h.assert_absent(c, "PING")                        -- the py-only override absent
  end)
end)