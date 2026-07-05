-- Enums -> name->value tables (mirrors tests/python/test_enums.py). A scoped enum is
-- reached as E.Value; an unscoped enum also mirrors its names onto the module.
local h = require("helper")
local m = h.mod.enums

describe("enums", function()
  it("binds a scoped automatic enum", function()
    assert.are.equal(0, m.Direction.North)
    assert.are.equal(1, m.Direction.East)
    assert.are.equal(3, m.Direction.West)  -- keeps its C++ value (South was excluded)
    assert.is_nil(m.Direction.South)       -- excluded enumerator
  end)

  it("mirrors an unscoped enum onto the module", function()
    assert.are.equal(0, m.Signal.Green)
    assert.are.equal(1, m.Signal.Yellow)
    assert.are.equal(2, m.Signal.Red)
    assert.are.equal(0, m.Green)           -- unscoped: also mirrored onto the module
    assert.are.equal(1, m.Yellow)
    assert.are.equal(2, m.Red)
  end)

  it("binds a scoped opt_in enum", function()
    assert.are.equal(0, m.Level.Debug)
    assert.are.equal(1, m.Level.Info)
    assert.is_nil(m.Level.Trace)           -- not opted in
  end)

  it("binds an enum-typed member", function()
    assert.are.equal(3, m.Compass.new(m.Direction.West).facing)
  end)
end)
