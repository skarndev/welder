-- Return-value policy in Lua (mirrors tests/python/test_retpolicy.py).
--
-- [[=welder::return_policy]] is a Python-binding concept: the Lua rods decide a
-- returned object's ownership *structurally* from the C++ return type (a value ->
-- a VM-owned copy; a reference -> a non-owning view), so they ignore the policy at
-- run time, exactly as they ignore [[=welder::doc]]. Both `view` (reference_internal)
-- and `snapshot` (copy) return `Inner&`, so in Lua both are live references — the
-- policy makes no difference here. (The copy/keep_alive divergence is asserted only
-- on the Python side, where the policy is honored.)
local h = require("helper")
local m = h.mod.retpolicy

describe("return_policy (structural in Lua)", function()
  it("hands back a live reference through view()", function()
    local o = m.Owner.new()
    o:view().v = 5
    assert.are.equal(5, o:inner_v())
  end)

  it("ignores the copy policy — snapshot() is a reference too", function()
    local o = m.Owner.new()
    o:snapshot().v = 7
    assert.are.equal(7, o:inner_v())
  end)
end)