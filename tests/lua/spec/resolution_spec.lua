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
    h.assert_absent(a, "only_py")        -- only(py): closed world, lua is out
    h.assert_absent(a, "only_then_excl") -- exclude beats only
  end)

  it("opt_in binds only lua-included members", function()
    local o = m.OptIn.new()
    o.incl_all = 2; assert.are.equal(2, o.incl_all)
    o.incl_lua = 3; assert.are.equal(3, o.incl_lua)
    o.only_lua = 4; assert.are.equal(4, o.only_lua) -- only(lua) is also the opt-in
    h.assert_absent(o, "unmarked")        -- opt_in, unmarked
    h.assert_absent(o, "incl_py")         -- included for py only
    h.assert_absent(o, "incl_then_excl")  -- exclude wins
    h.assert_absent(o, "only_py")         -- only(py): closed world, lua is out
  end)

  it("respects access control", function()
    local ac = m.Access.new()
    ac.visible = 7; assert.are.equal(7, ac.visible)
    assert.are.equal(9, ac:read_hidden())  -- public method reads a private field
    h.assert_absent(ac, "hidden")
    h.assert_absent(ac, "guarded")
  end)

  it("weld_protected admits protected members", function()
    local s = m.Shielded.new()
    assert.are.equal(40, s:base())        -- protected method
    assert.are.equal(42, s:total())
    assert.are.equal(7, m.Shielded.origin()) -- protected static
    assert.are.equal(6, s:scale(3))       -- protected overload group
    s.boost = 10                          -- protected data, read/write
    assert.are.equal(50, s:total())
    h.assert_absent(s, "tuning")          -- exclude still wins
    h.assert_absent(s, "core")            -- private: never bound
    h.assert_absent(s, "internal")
  end)

  it("weld_protected(py) does not reach lua", function()
    local s = m.ShieldedPy.new()
    s.visible = 3; assert.are.equal(3, s.visible)
    h.assert_absent(s, "guarded")  -- protected admitted for py only
    h.assert_absent(s, "peek")
  end)

  it("weld_protected composes with opt_in", function()
    local o = m.OptInShielded.new()
    o.chosen = 1; assert.are.equal(1, o.chosen)
    o.picked = 2; assert.are.equal(2, o.picked)  -- protected + include
    h.assert_absent(o, "unpicked")               -- visible but not opted in
  end)
end)
