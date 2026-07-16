-- Class-NESTED types (mirrors tests/python/test_nested.py). A type nested inside
-- a welded class resolves like any other member — the outer's policy + the nested
-- type's own marks, never a weld of its own — and is reached as M.Outer.Inner on
-- both Lua rods (sol2 places it on the usertype table; LuaBridge3 moves the class
-- table onto the outer as a raw static entry).
local h = require("helper")
local m = h.mod.nested

-- Reading an ABSENT key off a class table may return nil (sol2) or raise
-- (LuaBridge3's static-member guard); both mean "not bound".
local function absent(t, k)
  local ok, v = pcall(function() return t[k] end)
  return not ok or v == nil
end

describe("nested types", function()
  it("binds a nested class under the outer", function()
    assert.is_not_nil(m.Robot.Sensor)
    assert.is_true(absent(m, "Sensor")) -- scoped under the class, not the module
    local s = m.Robot.Sensor.new()
    assert.are.equal(1.5, s.range)
    s.range = 2.0
    assert.are.equal(4.0, s:doubled())
    -- the synthesized aggregate field constructor works for nested types too
    assert.are.equal(6.0, m.Robot.Sensor.new(3.0):doubled())
  end)

  it("round-trips members typed with nested types", function()
    local r = m.Robot.new()
    assert.are.equal(1.5, r.sensor.range)
    r:set_mode(m.Robot.Mode.active)
    assert.are.equal(m.Robot.Mode.active, r:get_mode())
    assert.are.equal(m.Robot.Mode.active, r.mode)
  end)

  it("binds a nested scoped enum under the class", function()
    assert.are.equal(2, m.Robot.Mode.fault)
    assert.is_true(absent(m.Robot, "fault")) -- scoped: values stay qualified
  end)

  it("mirrors a nested unscoped enum onto the class", function()
    assert.are.equal(1, m.Robot.Alarm.loud)
    assert.are.equal(0, m.Robot.quiet) -- mirrors C++'s Robot::quiet
  end)

  it("passes nested enums through methods", function()
    local r = m.Robot.new()
    assert.are.equal(m.Robot.Alarm.loud, r:alarm_for(m.Robot.Mode.fault))
    assert.are.equal(m.Robot.Alarm.quiet, r:alarm_for(m.Robot.Mode.idle))
  end)

  it("honors marks on nested types", function()
    assert.is_true(absent(m.Robot, "Hidden"))  -- excluded everywhere
    assert.is_true(absent(m.Robot, "Config"))  -- excluded for lua (Python binds it)
  end)

  it("recurses: nested-in-nested", function()
    assert.are.equal(9, m.Machine.Part.Bolt.new(9).size)
    assert.are.equal(5, m.Machine.new().part.bolt.size)
  end)

  it("binds only included nested types under opt_in", function()
    assert.are.equal(0, m.Panel.Knob.new().pos)
    assert.is_true(absent(m.Panel, "Wiring"))
  end)

  it("never binds a private nested type", function()
    assert.are.equal(2, m.Cabinet.new().drawers)
    assert.is_true(absent(m.Cabinet, "Stash"))
  end)
end)