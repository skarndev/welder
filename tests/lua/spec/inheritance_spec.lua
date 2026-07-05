-- Inheritance (mirrors tests/python/test_inheritance.py): welded bases, non-welded
-- mixin flattening, a welded base reached through a non-welded bridge, and — since
-- sol2 supports several bases — a multiple/virtual diamond.
local h = require("helper")
local m = h.mod.inheritance

describe("inheritance", function()
  it("inherits a welded base", function()
    local d = m.Derived.new()
    assert.are.equal(2, d.derived_field)
    assert.are.equal(2, d:derived_method())
    assert.are.equal(1, d.base_field)       -- inherited from the welded base
    assert.are.equal(1, d:base_method())
    h.assert_absent(d, "base_secret")
  end)

  it("walks a multilevel chain", function()
    local l = m.Leaf.new()                   -- Leaf -> Mid -> Base
    assert.are.equal(6, l.leaf_field)
    assert.are.equal(5, l.mid_field)
    assert.are.equal(1, l.base_field)
  end)

  it("flattens a non-welded mixin", function()
    local w = m.WithMixin.new()
    assert.are.equal(4, w.own_field)
    assert.are.equal(3, w.mixin_field)
    assert.are.equal(3, w:mixin_method())
    h.assert_absent(w, "mixin_secret")
    assert.is_nil(m.Mixin)                   -- a non-welded mixin is not its own type
  end)

  it("reaches a welded base through a non-welded bridge", function()
    local t = m.Through.new()
    assert.are.equal(12, t.through_field)
    assert.are.equal(11, t.bridge_field)     -- flattened bridge
    assert.are.equal(10, t.welded_field)     -- welded base survives the bridge
    assert.are.equal(10, t:welded_method())
  end)

  it("binds multiple and virtual bases", function()
    local b = m.Bottom.new()                 -- Bottom -> Left, Right -> virtual Apex
    assert.are.equal(23, b.bottom_field)
    assert.are.equal(21, b.left_field)
    assert.are.equal(22, b.right_field)
    assert.are.equal(20, b.apex_field)       -- shared virtual base
  end)
end)
