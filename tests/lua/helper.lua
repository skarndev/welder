-- Shared helpers for the welder sol2 (Lua) specs. `require` caches, so every spec
-- shares one module instance — the Lua counterpart of the pytest conftest fixture
-- that hands each test the same imported extension.
local M = {}

M.mod = require("welder_test_sol2")

-- A sol2 usertype built with SOL_ALL_SAFETIES_ON may either return nil or raise on
-- an unknown member, so tolerate both when asserting a member did not bind. Uses
-- luassert's callable `assert(value, message)` (busted exposes it as a global).
function M.assert_absent(obj, key)
  local ok, v = pcall(function() return obj[key] end)
  assert((not ok) or (v == nil),
    ("member %q should be absent"):format(tostring(key)))
end

return M
