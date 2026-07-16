-- Class-template instantiations welded through namespace-scope aliases (mirrors
-- tests/python/test_templates.py). members_of(ns) enumerates the template, never an
-- instantiation, so `using IntCrate = Crate<int>;` is the way one enters the sweep:
-- the alias is both the C++ spelling and the default Lua name.
local h = require("helper")
local m = h.mod.templates

describe("templates", function()
  it("an alias welds an instantiation", function()
    local c = m.IntCrate.new()
    assert.are.equal(0, c:get())
    c:put(7)
    assert.are.equal(7, c:get())
    assert.are.equal(7, c.item)          -- the field binds like any welded type's
    h.assert_absent(m, "Crate")          -- the bare template never binds
  end)

  it("two instantiations of one template bind independently", function()
    local w = m.WordCrate.new()
    w:put("hello")
    assert.are.equal("hello", w:get())
    assert.are.equal(0, m.IntCrate.new():get())
  end)

  it("weld_as on the alias renames verbatim", function()
    local d = m.CrateOfDouble.new()
    h.assert_absent(m, "RenamedCrate")   -- the identifier is not used
    d:put(2.5)
    assert.are.equal(2.5, d:get())
  end)

  it("weld_as on the template applies through the alias", function()
    assert.is_not_nil(m.TaggedBox)
    h.assert_absent(m, "TaggedInt")
    assert.are.equal(0, m.TaggedBox.new().tag)
  end)

  it("a weld on the alias opts in an unwelded template", function()
    local p = m.IntPack.new()
    assert.are.equal(0, p:unwrap())
    p.payload = 9
    assert.are.equal(9, p:unwrap())
  end)

  it("binds nested types of an alias-welded instantiation", function()
    -- Silo<int>'s nested class/enum resolve under the instantiation (no weld of
    -- their own) and bind under the ALIAS's name; the flip() signature passes
    -- the gate (the template's weld is read through the instantiation).
    local s = m.IntSilo.new()
    assert.are.equal(4, s.hatch.width)
    assert.are.equal(m.IntSilo.State.shut, s:flip(m.IntSilo.State.open))
  end)

  it("binds a nested type of an alias-opted-in template", function()
    -- The weld on the IntPack alias also brings the vendor template's nested
    -- type along — it resolves under the instantiation like any member.
    assert.are.equal(1, m.IntPack.Lid.new().fits)
  end)

  it("weld_protected reaches alias-welded instantiations", function()
    -- policy::weld_protected sits on the Vault TEMPLATE, read through the
    -- instantiation like every annotation.
    local v = m.IntVault.new()
    v:stash(5)                           -- protected method
    assert.are.equal(5, v:peek())
    assert.are.equal(5, v.locked)        -- protected data
    h.assert_absent(v, "combination")    -- private: never bound
  end)
end)