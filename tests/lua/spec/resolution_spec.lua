-- Resolution: policy + per-member marks (mirrors tests/python/test_resolution.py).
-- Where a case carries per-language marks, Lua sees the lua-resolved binding, which
-- differs from Python by design (e.g. a py-only exclusion is bound here).
local h = require("helper")
local m = h.mod.resolution

describe("resolution", function()
  it("automatic binds unless excluded", function()
    local a = m.Automatic.new()
    a.kept = 5;    assert.are.equal(5, a.kept)
    a.excl_py = 9; assert.are.equal(9, a.excl_py) -- excluded for py only -> bound for lua
    a.incl_py = 1; assert.are.equal(1, a.incl_py) -- redundant include -> automatic binds
  end)

  it("automatic drops all- and lua-excluded members", function()
    local a = m.Automatic.new()
    h.assert_absent(a, "excl_all")
    h.assert_absent(a, "excl_lua")
  end)

  it("opt_in binds only lua-included members", function()
    local o = m.OptIn.new()
    o.incl_all = 2; assert.are.equal(2, o.incl_all)
    o.incl_lua = 3; assert.are.equal(3, o.incl_lua)
    h.assert_absent(o, "unmarked")        -- opt_in, unmarked
    h.assert_absent(o, "incl_py")         -- included for py only
    h.assert_absent(o, "incl_then_excl")  -- exclude wins
  end)

  it("respects access control", function()
    local ac = m.Access.new()
    ac.visible = 7; assert.are.equal(7, ac.visible)
    assert.are.equal(9, ac:read_hidden())  -- public method reads a private field
    h.assert_absent(ac, "hidden")
    h.assert_absent(ac, "guarded")
  end)
end)
