-- Method-backed properties (getter/setter marks), asserted through the Lua
-- module (mirrors tests/python/test_properties.py; same sections, same order).
-- Where a mark is language-scoped the Lua view differs from Python by design:
-- Gauge's py-scoped accessors stay ordinary methods here, and Gauge.limit is
-- read-only here (its setter is excluded for lua).
local h = require("helper")
local m = h.mod.properties

describe("properties", function()
  -- overload-style accessors: radius() / radius(double)
  it("binds an overload-style pair as one read/write property", function()
    local c = m.Circle.new()
    assert.are.equal(1.0, c.radius)
    c.radius = 2.0
    assert.are.equal(2.0, c.radius)
  end)

  it("binds a lone prefix-style getter read-only", function()
    local c = m.Circle.new()
    c.radius = 2.0
    assert.is_true(math.abs(c.area - 12.566) < 1e-3)
    assert.has_error(function() c.area = 5.0 end)
  end)

  it("keeps ordinary methods as methods", function()
    local c = m.Circle.new()
    assert.are.equal(2.0, c:diameter())
    h.assert_absent(c, "get_area") -- the accessor spelling became the property
  end)

  -- name derivation across spelling conventions
  it("derives camelCase names in their own convention", function()
    local v = m.Vehicle.new()
    assert.are.equal(60, v.maxSpeed)
    v.maxSpeed = 10
    assert.are.equal(10, v.maxSpeed)
  end)

  it("pairs mixed-convention accessors under the getter's spelling", function()
    local v = m.Vehicle.new() -- get_scale + SetScale -> one property "scale"
    assert.are.equal(1.0, v.scale)
    v.scale = 2.5
    assert.are.equal(2.5, v.scale)
    h.assert_absent(v, "SetScale")
  end)

  it("does not strip an is_ predicate", function()
    local v = m.Vehicle.new()
    assert.is_true(v.is_ready)
    assert.has_error(function() v.is_ready = false end) -- read-only
  end)

  -- explicit names + per-language scoping
  it("pairs explicit names regardless of the identifiers", function()
    local g = m.Gauge.new()
    assert.are.equal(0.5, g.level)
    g.level = 0.9
    assert.is_true(math.abs(g.level - 0.9) < 1e-9)
    h.assert_absent(g, "raw")
    h.assert_absent(g, "assign")
  end)

  it("keeps py-scoped accessors as ordinary Lua methods", function()
    local g = m.Gauge.new()
    assert.are.equal(1, g:get_mode())
    g:set_mode(3)
    assert.are.equal(3, g:get_mode())
  end)

  it("makes a property read-only where its setter is excluded", function()
    local g = m.Gauge.new() -- set_limit carries mark::exclude(lua)
    assert.are.equal(100, g.limit)
    assert.has_error(function() g.limit = 5 end)
  end)

  -- opt_in: an accessor mark implies the opt-in
  it("treats accessor marks as the opt-in under opt_in", function()
    local p = m.Padlock.new()
    assert.are.equal(1234, p.code)
    p.code = 999
    assert.are.equal(999, p.code)
    h.assert_absent(p, "stray")          -- unmarked -> not bound
    assert.are.equal(42, p:shown())      -- a normal method still needs include
  end)

  -- flattened non-welded base + a fluent (value-returning) setter
  it("flattens base accessors into derived properties", function()
    local t = m.Tag.new()
    assert.are.equal("blank", t.label)
    t.label = "hi" -- the fluent setter's return is discarded
    assert.are.equal("hi", t.label)
  end)

  -- protected accessors under weld_protected
  it("binds protected accessors under weld_protected", function()
    local s = m.Sealed.new()
    assert.are.equal(7, s.inner)
    s.inner = 12
    assert.are.equal(12, s.inner)
  end)
end)