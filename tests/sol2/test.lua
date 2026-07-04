-- Lua assertions for the welder sol2 backend, run against the module built from
-- the shared C++ case tree (tests/common/cpp) — the Lua counterpart of the
-- tests/test_*.py specs. Where a case carries per-language marks, Lua sees the
-- lua-resolved binding (which differs from Python by design): e.g. members
-- excluded only for py are bound here, and vice versa.

local m = require("welder_test_sol2")

-- A usertype instance rejects unknown members (SOL_ALL_SAFETIES_ON), so probe
-- absence through pcall: absent iff reading errors or yields nil.
local function absent(obj, key)
  local ok, v = pcall(function() return obj[key] end)
  return (not ok) or (v == nil)
end

-- ============================ resolution =================================
do
  local r = m.resolution
  local a = r.Automatic.new()
  a.kept = 5;    assert(a.kept == 5)          -- bound
  a.excl_py = 9; assert(a.excl_py == 9)       -- excluded for py only -> bound for lua
  a.incl_py = 1; assert(a.incl_py == 1)       -- redundant include -> automatic binds
  assert(absent(a, "excl_all"))               -- excluded everywhere
  assert(absent(a, "excl_lua"))               -- excluded for lua

  local o = r.OptIn.new()
  o.incl_all = 2; assert(o.incl_all == 2)     -- included everywhere
  o.incl_lua = 3; assert(o.incl_lua == 3)     -- included for lua
  assert(absent(o, "unmarked"))               -- opt_in, unmarked
  assert(absent(o, "incl_py"))                -- included for py only
  assert(absent(o, "incl_then_excl"))         -- exclude wins

  local ac = r.Access.new()
  ac.visible = 7; assert(ac.visible == 7)     -- public data
  assert(ac:read_hidden() == 9)               -- public method reads private
  assert(absent(ac, "hidden") and absent(ac, "guarded"))
end

-- ============================ methods ===================================
do
  local mm = m.methods
  local c = mm.Counter.new()
  assert(c:value() == 0)
  c:increment(); assert(c:value() == 1)
  c:add(10);     assert(c:value() == 11)
  assert(mm.Counter.new(5):value() == 5)      -- overloaded ctor
  assert(mm.Counter(3):value() == 3)          -- call-constructor form
  assert(mm.Counter.version() == 7)           -- static -> class-table function
  assert(absent(c, "secret"))                 -- excluded method

  assert(mm.Vec2.new().x == 0.0)              -- aggregate default ctor
  local v = mm.Vec2.new(3.0, 4.0)             -- synthesized field ctor (paren-init)
  assert(v.x == 3.0 and v.y == 4.0)
end

-- ========================== inheritance =================================
do
  local ih = m.inheritance
  local d = ih.Derived.new()
  assert(d.derived_field == 2 and d:derived_method() == 2)
  assert(d.base_field == 1 and d:base_method() == 1)   -- from welded base
  assert(absent(d, "base_secret"))

  local l = ih.Leaf.new()
  assert(l.leaf_field == 6 and l.mid_field == 5 and l.base_field == 1)

  local w = ih.WithMixin.new()
  assert(w.own_field == 4 and w.mixin_field == 3 and w:mixin_method() == 3)
  assert(absent(w, "mixin_secret"))
  assert(ih.Mixin == nil)                     -- non-welded mixin is not its own type

  local t = ih.Through.new()
  assert(t.through_field == 12 and t.bridge_field == 11) -- flattened bridge
  assert(t.welded_field == 10 and t:welded_method() == 10) -- welded base through bridge

  -- Multiple + virtual inheritance (sol2 supports several bases).
  local b = ih.Bottom.new()
  assert(b.bottom_field == 23 and b.left_field == 21 and b.right_field == 22)
  assert(b.apex_field == 20)                  -- shared virtual base
end

-- ============================ namespace =================================
do
  local cat = m.catalog
  assert(cat.Item.new(3):get_id() == 3 and cat.Item.new().id == 0)
  assert(cat.Hidden == nil)                   -- not welded
  assert(cat.Cat.new().whiskers == 12 and cat.Cat.new().legs == 4)

  assert(cat.total(7) == 7)                   -- last overload wins (sol2 limitation)
  assert(cat.internal_helper == nil)          -- not welded
  assert(cat.suppressed() == -1)              -- excluded for py only -> bound for lua

  assert(cat.LIMIT == 100)                    -- constexpr snapshot
  assert(cat.TAG == "catalog")                -- const std::string -> lua string
  assert(cat.PRIVATE_LIMIT == nil)            -- not welded
  assert(cat.counter == 0)                    -- mutable var: value snapshot
  assert(type(cat.bump) == "function")        -- (liveness is a planned enhancement)

  assert(cat.sub.Nested.new().v == 5)         -- nested namespace -> submodule
  assert(cat.quiet == nil)                    -- no welded content
  assert(cat.strict == nil)                   -- opt_in members only include(py) -> none for lua
  assert(cat.secret.Spy.new().s == 0)         -- namespace excluded for py only -> present for lua
end

-- ============================ operators =================================
do
  local op = m.operators
  local v = op.Vec.new(1.0, 2.0)
  assert(v.x == 1.0 and v.y == 2.0)
  assert((v + op.Vec.new(3.0, 4.0)).x == 4.0) -- __add
  assert((v - op.Vec.new(1.0, 1.0)).y == 1.0) -- __sub (binary)
  assert((-v).x == -1.0)                      -- __unm (unary minus)
  assert((v * 2.0).x == 2.0)                  -- __mul
  assert(v == op.Vec.new(1.0, 2.0))           -- __eq
  assert(v ~= op.Vec.new(9.0, 9.0))           -- Lua derives ~= from __eq
  assert(v[0] == 1.0 and v[1] == 2.0)         -- operator[] -> __index fallback
  assert(v.x == 1.0)                          -- ...and member access still works

  local sum = op.Meters.new(1.0) + op.Feet.new(1.0)  -- heterogeneous, Feet welded
  assert(math.abs(sum.value - 1.3048) < 1e-9)

  assert(op.Coin.new(5).cents == 5)           -- free operator+ not bound; value ok
  assert((op.OpAutomatic.new(2) + op.OpAutomatic.new(3)).v == 5) -- __add bound
  assert((op.OpOptIn.new(2) + op.OpOptIn.new(3)).v == 5)         -- included __add
end

-- ============================== enums ===================================
do
  local en = m.enums
  assert(en.Direction.North == 0 and en.Direction.East == 1 and en.Direction.West == 3)
  assert(en.Direction.South == nil)           -- excluded enumerator
  assert(en.Signal.Green == 0 and en.Signal.Yellow == 1 and en.Signal.Red == 2)
  assert(en.Green == 0 and en.Yellow == 1 and en.Red == 2) -- unscoped: mirrored onto module
  assert(en.Level.Debug == 0 and en.Level.Info == 1)       -- opt_in enumerators
  assert(en.Level.Trace == nil)               -- not opted in
  assert(en.Compass.new(en.Direction.West).facing == 3)    -- enum-typed member
end

print("welder sol2: all Lua assertions passed")
