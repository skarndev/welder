-- Cookbook 07 — assert the Lua face of the shared `journal` library.
-- Same library as check.py asserts for Python; compare the two side by side.

local journal = require("journal")

-- snake_case style: EVERY name reshaped for Lua, classes included.
local e = journal.entry("day 1", "welded")
assert(e:render_line() == "day 1: welded")

local nb = journal.notebook()
nb:add_entry(e)
nb:add_entry(journal.make_entry("day 2"))
assert(nb:entry_count() == 2)

-- Per-language weld_as: Lua spells renderAll `as_string` (Python: `to_text`).
assert(nb:as_string() == "day 1: welded\nday 2: (empty)\n")
assert(nb.to_text == nil)

-- The Lua-flavored save_to (mark::only(lua)) takes a writer callback.
local lines = {}
nb:save_to(function(line) table.insert(lines, line) end)
assert(#lines == 2)
assert(lines[1] == "day 1: welded")
assert(lines[2] == "day 2: (empty)")

assert(journal.format_version == 1)

-- Lua has no runtime docstrings; the LuaCATS stub carries them instead.
local stub_path = os.getenv("JOURNAL_LUACATS")
local f = assert(io.open(stub_path, "r"))
local stub = f:read("a")
f:close()
assert(stub:find("---@meta", 1, true))
assert(stub:find("journal.notebook", 1, true))    -- styled class name
assert(stub:find("save_to", 1, true))             -- the lua-only flavor is typed
assert(stub:find("dated journal entry", 1, true)) -- the doc text survives

print("cookbook 07-multilang (lua): OK")