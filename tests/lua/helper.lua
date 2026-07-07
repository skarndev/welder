-- Shared helpers for the welder Lua specs. `require` caches, so every spec shares
-- one module instance — the Lua counterpart of the pytest conftest fixture that
-- hands each test the same imported extension.
--
-- The same specs run against every Lua rod (sol2, LuaBridge3) as a cross-backend
-- consistency check; the rod's module name is selected at run time by the
-- WELDER_TEST_LUA_MODULE environment variable (set per CTest), defaulting to the
-- sol2 module — the Lua analogue of the Python specs' WELDER_TEST_MODULE.
local M = {}

M.mod = require(os.getenv("WELDER_TEST_LUA_MODULE") or "welder_test_sol2")

-- A Lua binding built with runtime safety checks may either return nil or raise on
-- an unknown member, so tolerate both when asserting a member did not bind. Uses
-- luassert's callable `assert(value, message)` (busted exposes it as a global).
function M.assert_absent(obj, key)
  local ok, v = pcall(function() return obj[key] end)
  assert((not ok) or (v == nil),
    ("member %q should be absent"):format(tostring(key)))
end

return M
