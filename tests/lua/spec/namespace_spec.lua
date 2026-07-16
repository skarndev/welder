-- Whole-namespace binding (mirrors tests/python/test_namespace.py): classes, free
-- functions, variables, nested/pruned sub-namespaces. Several cases are pruned for
-- py but present for lua (suppressed, secret) or vice versa (strict), by design.
local h = require("helper")
local m = h.mod.catalog

describe("namespace", function()
  it("binds classes", function()
    assert.are.equal(3, m.Item.new(3):get_id())
    assert.are.equal(0, m.Item.new().id)
    assert.is_nil(m.Hidden)                    -- not welded
    assert.are.equal(12, m.Cat.new().whiskers)
    assert.are.equal(4, m.Cat.new().legs)      -- inherited from welded base
  end)

  it("binds free functions", function()
    assert.are.equal(7, m.total(7))            -- last overload wins (sol2 limitation)
    assert.is_nil(m.internal_helper)           -- not welded
    assert.are.equal(-1, m.suppressed())       -- excluded for py only -> bound for lua
  end)

  it("binds variables", function()
    assert.are.equal(100, m.LIMIT)             -- constexpr snapshot
    assert.are.equal("catalog", m.TAG)         -- const std::string -> lua string
    assert.is_nil(m.PRIVATE_LIMIT)             -- not welded
    assert.are.equal(0, m.counter)             -- mutable var: initial live value
    assert.is_function(m.bump)
  end)

  it("exposes mutable variables as live get/set", function()
    local start = m.counter
    m.bump()
    m.bump()
    assert.are.equal(start + 2, m.counter)     -- live read: reflects the C++ global
    m.counter = 500                            -- live write: flows back to C++
    m.bump()                                   -- C++ increments the same global
    assert.are.equal(501, m.counter)
  end)

  it("binds nested and pruned namespaces", function()
    assert.are.equal(5, m.sub.Nested.new().v)  -- nested namespace -> submodule table
    assert.is_nil(m.quiet)                     -- no welded content
    assert.is_nil(m.strict)                    -- opt_in members only include(py) -> none for lua
    assert.are.equal(0, m.secret.Spy.new().s)  -- namespace excluded for py only -> present for lua
  end)
end)

-- Semi-manual binding: hand-picked entities welded one at a time (weld_function /
-- weld_variable) onto the `manual` table, without welding a whole namespace.
describe("freestanding", function()
  local man = h.mod.manual

  it("binds a single free function via weld_function", function()
    assert.are.equal(42, man.scale(6, 7))
    assert.is_function(man.manual_bump)
  end)

  it("binds a constant via weld_variable", function()
    assert.are.equal(42, man.MANUAL_CONST)     -- constexpr snapshot
    assert.are.equal(0, man.manual_counter)    -- mutable var: initial live value
  end)

  it("binds a mutable variable as live get/set via weld_variable", function()
    man.manual_bump()
    assert.are.equal(1, man.manual_counter)    -- live read after C++ mutation
    man.manual_counter = 41                     -- live write through to C++
    man.manual_bump()
    assert.are.equal(42, man.manual_counter)
  end)

  it("honours a call-site name override", function()
    assert.are.equal(6, man.renamed_fn(5))    -- verbatim name beats identifier/weld_as
    assert.are.equal(99, man.RENAMED_CONST)
    assert.is_nil(man.renamable)              -- bound only under the override name
    assert.is_nil(man.RENAMABLE)
  end)
end)

-- Tack welding: an *unmarked* C++ namespace (`foreign`, no weld() anywhere) bound
-- greedily by the tack-welding carriage.
describe("tack (unmarked library)", function()
  local f = h.mod.foreign

  it("binds an unmarked class + method", function()
    assert.are.equal(3, f.Widget.new().size)
    assert.are.equal(6, f.Widget.new():doubled())
  end)

  it("binds an unmarked nested type", function()
    -- greedy sweeps member types like stitch: Widget.Stat registers under
    -- Widget, and stats()' signature passes the gate without a trust hatch.
    assert.are.equal(0, f.Widget.Stat.new().uses)
    assert.are.equal(3, f.Widget.new():stats().uses)
  end)

  it("never sweeps member aliases under tack", function()
    -- every complete type passes the greedy gate, so a member alias has
    -- nothing left to register.
    local ok, v = pcall(function() return f.Widget.Twin end)
    assert.is_true(not ok or v == nil)
  end)

  it("binds an unmarked free function and constant", function()
    assert.are.equal(7, f.add(2, 5))
    assert.are.equal(7, f.VERSION)
  end)

  it("recurses an unmarked nested namespace", function()
    assert.are.equal(5, f.nested.Gadget.new().id)
  end)

  it("accepts the library's own types in signatures", function()
    -- No trust_bindable hatch: the greedy registration oracle accepts class
    -- types the same tack pass registers (incl. a forward-declared one).
    local a, b = f.Widget.new(), f.Widget.new()
    assert.are.equal(6, a:merged(b).size)
    local c = f.fuse(a, b)
    assert.are.equal(3, c.left.size)
    assert.are.equal(3, c.right.size)
    assert.are.equal(5, f.gadget_id(f.nested.Gadget.new()))
  end)

  it("the WeldProtected knob admits an unmarked library's protected members", function()
    -- foreign_protected is tacked with greedy_resolution<true> — the blanket
    -- opt-in for a library that cannot carry policy::weld_protected.
    local p = h.mod.foreign_protected.Panel.new()
    assert.are.equal(10, p:trim())        -- protected method, bound by the knob
    assert.are.equal(4, p.width)          -- protected data, bound read/write
    p.width = 6
    assert.are.equal(16, p:frame())
    h.assert_absent(p, "serial")          -- private: out under every knob/resolution
  end)

  it("the resolution hook receives the bound-into entity", function()
    -- foreign_mixed's resolution admits protected members only when
    -- bound_into == Display: Meter DECLARES reading(), Display inherits it
    -- (flattened). Present on Display, absent on Meter = the carriage hands
    -- the hook the welded type, not the declaring class.
    local fm = h.mod.foreign_mixed
    local d = fm.Display.new()
    assert.are.equal(55, d:reading())
    assert.are.equal(1, d:model())
    local mtr = fm.Meter.new()
    assert.are.equal(1, mtr:model())
    h.assert_absent(mtr, "reading")
  end)
end)
