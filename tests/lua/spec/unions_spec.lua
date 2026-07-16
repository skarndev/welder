-- Welder's union stance (mirrors tests/python/test_unions.py). Unions never
-- bind — reading an inactive member is UB in C++ — and every attempt is a
-- designed hard compile error. These specs assert the escape hatches and the
-- blessed path, std::variant, which crosses every rod as a plain value.
-- C++ side: tests/common/cpp/unions.hpp.
local h = require("helper")
local m = h.mod.unions

describe("unions", function()
  it("a plain union in the namespace does not bind", function()
    h.assert_absent(m, "Payload")
  end)

  it("an excluded union member is absent; safe accessors still work", function()
    local p = m.Packet.new()
    assert.are.equal(0, p.kind)
    h.assert_absent(p, "payload")
    -- `code` reads the member C++ knows is active (brace-init of a union
    -- initializes its first member) — the accessor-function remedy.
    assert.are.equal(7, p:code())
  end)

  it("anonymous union members are skipped; named neighbors bind", function()
    local f = m.Frame.new()
    assert.are.equal(1, f.tag)
    assert.are.equal(2, f.checksum)
    h.assert_absent(f, "raw")
    h.assert_absent(f, "scaled")
  end)

  it("std.variant crosses as a value, both alternatives", function()
    local holder = m.Holder.new()
    assert.are.equal(0, holder.value) -- defaults to the first alternative
    holder.value = 5
    assert.are.equal(5, holder.value)
    holder.value = m.Boxed.new(9)
    assert.are.equal(9, holder.value.n)
  end)

  it("a variant signature converts both ways", function()
    assert.are.equal(3, m.box_if(false, 3))
    assert.are.equal(3, m.box_if(true, 3).n)
  end)
end)