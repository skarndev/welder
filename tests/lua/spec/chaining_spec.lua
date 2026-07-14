-- Handle chaining: hand-written backend registrations on the handles welder's
-- entry points return (mirrors tests/python/test_chaining.py; C++ side:
-- chaining.hpp). The function-alias case exists only for rods that return a
-- function handle — sol2 does; LuaBridge3's fluent registrar has none.
local h = require("helper")
local m = h.mod.chaining

describe("chaining", function()
  it("hand-registered method rides the returned class handle", function()
    local g = m.Gadget.new()
    g.n = 21
    assert.are.equal(42, g:doubled())
  end)

  it("welded free function binds", function()
    assert.are.equal(6, m.twice(3))
  end)

  it("aliases the returned function handle (handle-returning rods)", function()
    if m.twice_alias == nil then
      return -- LuaBridge3: weld_function is void (no per-function handle)
    end
    assert.are.equal(8, m.twice_alias(4))
  end)
end)