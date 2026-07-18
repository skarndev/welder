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
    assert.are.equal(5, (m.OpAutomatic.new(2) + m.OpAutomatic.new(3)).v) -- __add bound
    assert.are.equal(5, (m.OpOptIn.new(2) + m.OpOptIn.new(3)).v)         -- included __add
  end)

  it("binds anchored free operators", function()
    -- Coin's operator+ is a free (namespace-scope) function: the sweep anchors
    -- it on Coin and it lands in __add like a member operator.
    assert.are.equal(3, (m.Coin.new(1) + m.Coin.new(2)).cents)
    -- ...while the mark::exclude'd free operator- resolves out.
    assert.has_error(function() return m.Coin.new(3) - m.Coin.new(1) end)
  end)

  it("merges member and free operators into one slot", function()
    -- Mixed has a member operator+(Mixed) AND a free operator+(Mixed, int): one
    -- combined __add overload set holds both (one value per metamethod slot).
    assert.are.equal(7, (m.Mixed.new(3) + m.Mixed.new(4)).v)
    assert.are.equal(7, (m.Mixed.new(3) + 4).v)
  end)

  it("dispatches either operand order of a free operator", function()
    -- operator*(double, Scaled) and operator*(Scaled, double) both live in
    -- __mul; Lua passes the operands as written and the signatures dispatch.
    assert.are.equal(6.0, (m.Scaled.new(3.0) * 2.0).f)
    assert.are.equal(6.0, (2.0 * m.Scaled.new(3.0)).f)
  end)

  it("binds the free ostream inserter as __tostring", function()
    assert.are.equal("Scaled(2.5)", tostring(m.Scaled.new(2.5)))
  end)

  it("synthesizes __lt/__le from a defaulted spaceship", function()
    -- Lua derives >, >= and ~= itself, so two slots give the full relational
    -- set; the defaulted <=>'s implicit operator== binds __eq alongside.
    assert.is_true(m.Version.new(1, 2) < m.Version.new(1, 3))
    assert.is_true(m.Version.new(1, 2) <= m.Version.new(1, 2))
    assert.is_true(m.Version.new(2, 0) > m.Version.new(1, 9))
    assert.is_true(m.Version.new(2, 0) >= m.Version.new(2, 0))
    assert.is_true(m.Version.new(1, 2) == m.Version.new(1, 2))
    assert.is_true(m.Version.new(1, 2) ~= m.Version.new(1, 3))
  end)

  it("synthesizes relationals from a custom spaceship", function()
    assert.is_true(m.Temp.new(1.0) < m.Temp.new(2.0))
    assert.is_true(m.Temp.new(2.0) > m.Temp.new(1.0))
    assert.is_true(m.Temp.new(1.0) <= m.Temp.new(1.0))
    -- No operator== declared, none synthesized: userdata identity applies.
    assert.is_false(m.Temp.new(1.0) == m.Temp.new(1.0))
  end)

  it("synthesizes heterogeneous comparisons in both operand orders", function()
    local a = m.Account.new(10)
    assert.is_true(a < 20)  -- __lt(Account, int)
    assert.is_true(a > 5)   -- 'a > 5' is '5 < a': __lt(int, Account)
    assert.is_true(5 < a)
    assert.is_true(20 > a)
    -- (a == 10 stays false: Lua only consults __eq when BOTH operands are
    -- userdata of the same kind — the Python specs cover the int ==.)
  end)

  it("lets an explicit operator beat synthesis slot by slot", function()
    assert.is_true(m.Ordered.new(2) < m.Ordered.new(1))  -- the inverted explicit <
    -- Lua has no __gt: `a > b` IS `b < a`, so > mirrors the (inverted) explicit
    -- < — unlike Python/C++, where > synthesizes from <=> independently. A
    -- deliberate semantic seam of Lua's metamethod model, not of welder's.
    assert.is_true(m.Ordered.new(1) > m.Ordered.new(2))
    assert.is_true(m.Ordered.new(1) <= m.Ordered.new(2)) -- synthesized from <=>
  end)

  it("honors per-language marks on the spaceship", function()
    -- PyOnlyCmp's <=> is exclude(lua)'d: no __lt here (the Python tests assert
    -- the presence on their side).
    assert.has_error(function() return m.PyOnlyCmp.new(1) < m.PyOnlyCmp.new(2) end)
  end)
end)
