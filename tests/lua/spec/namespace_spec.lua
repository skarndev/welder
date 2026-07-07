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

  it("binds an unmarked free function and constant", function()
    assert.are.equal(7, f.add(2, 5))
    assert.are.equal(7, f.VERSION)
  end)

  it("recurses an unmarked nested namespace", function()
    assert.are.equal(5, f.nested.Gadget.new().id)
  end)
end)
