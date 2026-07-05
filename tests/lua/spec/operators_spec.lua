-- Operators -> Lua metamethods (mirrors tests/python/test_operators.py). Lua has a
-- smaller/asymmetric set: ~= is derived from __eq (no __ne), and operator[] rides
-- __index alongside normal member access.
local h = require("helper")
local m = h.mod.operators

describe("operators", function()
  it("binds arithmetic metamethods", function()
    local v = m.Vec.new(1.0, 2.0)
    assert.are.equal(4.0, (v + m.Vec.new(3.0, 4.0)).x) -- __add
    assert.are.equal(1.0, (v - m.Vec.new(1.0, 1.0)).y) -- __sub (binary)
    assert.are.equal(-1.0, (-v).x)                     -- __unm (unary minus)
    assert.are.equal(2.0, (v * 2.0).x)                 -- __mul
  end)

  it("derives comparison metamethods", function()
    local v = m.Vec.new(1.0, 2.0)
    assert.is_true(v == m.Vec.new(1.0, 2.0))           -- __eq
    assert.is_true(v ~= m.Vec.new(9.0, 9.0))           -- Lua derives ~= from __eq
  end)

  it("coexists subscript with member access", function()
    local v = m.Vec.new(1.0, 2.0)
    assert.are.equal(1.0, v[0])                         -- operator[] -> __index fallback
    assert.are.equal(2.0, v[1])
    assert.are.equal(1.0, v.x)                          -- ...member access still works
  end)

  it("handles heterogeneous operands and resolution", function()
    local sum = m.Meters.new(1.0) + m.Feet.new(1.0)    -- Feet welded -> converts
    assert.near(1.3048, sum.value, 1e-9)
    assert.are.equal(5, m.Coin.new(5).cents)           -- free operator+ not bound; value ok
    assert.are.equal(5, (m.OpAutomatic.new(2) + m.OpAutomatic.new(3)).v) -- __add bound
    assert.are.equal(5, (m.OpOptIn.new(2) + m.OpOptIn.new(3)).v)         -- included __add
  end)
end)
