-- Per-overload / per-constructor marks (mirrors tests/python/test_overloads.py).
-- The Lua side sees a DIFFERENT participating set by design: the overloads marked
-- exclude(lua) are absent here while Python keeps them — proof that each overload
-- resolves independently and the group a Lua rod registers is exactly the
-- resolution's verdict (one table slot per name, never a stale sibling).
local h = require("helper")
local m = h.mod.overloads

describe("overload marks", function()
  it("dispatches the surviving method overloads", function()
    local c = m.Calc.new(5)
    assert.are.equal(7, c:apply(1)) -- value + x + 1
  end)

  it("drops the per-language excluded method overload", function()
    local c = m.Calc.new(5)
    assert.has_error(function() return c:apply("x", 2) end) -- exclude(lua)
  end)

  it("drops the everywhere-excluded method overload", function()
    local c = m.Calc.new(5)
    assert.has_error(function() return c:apply("a", "b") end)
  end)

  it("binds the surviving constructors and drops the excluded one", function()
    assert.are.equal(0, m.Calc.new().value)
    assert.are.equal(9, m.Calc.new(9).value)
    assert.has_error(function() return m.Calc.new("a", "b") end)
  end)

  it("resolves free-function overloads per overload", function()
    assert.are.equal(4, m.pick(4))
    assert.has_error(function() return m.pick("s") end) -- exclude(lua)
  end)

  it("resolves opt_in constructors symmetrically (default ctor exempt)", function()
    assert.are.equal(0, m.OptInCtor.new().kept)                       -- default: exempt
    assert.has_error(function() return m.OptInCtor.new(7) end)        -- unmarked: filtered
    local o = m.OptInCtor.new(3, 4)                                   -- included: bound
    assert.are.equal(7, o.kept)
    assert.is_nil(o.hidden)
  end)

  it("supports an explicit factory-only surface", function()
    assert.has_error(function() return m.FactoryOnly.new(1) end) -- all ctors excluded
    assert.are.equal(9, m.forge(9).id)                           -- instances from C++
  end)

  it("honors exclude on a declared default constructor", function()
    assert.has_error(function() return m.NoDefault.new() end)
    assert.are.equal(5, m.NoDefault.new(5).v)
  end)
end)