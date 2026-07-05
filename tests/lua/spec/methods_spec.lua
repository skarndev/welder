-- Constructors, methods, aggregates (mirrors tests/python/test_methods.py).
local h = require("helper")
local m = h.mod.methods

describe("methods", function()
  it("binds constructors and methods", function()
    local c = m.Counter.new()
    assert.are.equal(0, c:value())
    c:increment(); assert.are.equal(1, c:value())
    c:add(10);     assert.are.equal(11, c:value())
    assert.are.equal(5, m.Counter.new(5):value()) -- overloaded ctor (.new form)
    assert.are.equal(3, m.Counter(3):value())     -- call-constructor form
    assert.are.equal(7, m.Counter.version())      -- static -> class-table function
    h.assert_absent(c, "secret")                  -- excluded method
  end)

  it("dispatches overloaded methods", function()
    -- Calc.sum has two C++ overloads (1-arg, 2-arg); sol2 groups them into a
    -- sol::overload so both resolve, rather than the last one winning.
    local c = m.Calc(10)
    assert.are.equal(15, c:sum(5))    -- one-arg overload -> base + a
    assert.are.equal(20, c:sum(5, 5)) -- two-arg overload -> base + a + b
  end)

  it("synthesizes aggregate constructors", function()
    assert.are.equal(0.0, m.Vec2.new().x)         -- default ctor
    local v = m.Vec2.new(3.0, 4.0)                -- synthesized field ctor (paren-init)
    assert.are.equal(3.0, v.x)
    assert.are.equal(4.0, v.y)
  end)
end)
