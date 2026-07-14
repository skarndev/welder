# Cookbook 07 — multilang: one annotated header shipped to Python (nanobind) and
# Lua (sol2 AND LuaBridge3 — one language, two frameworks), with a name style per
# language, a .pyi stub and a LuaCATS stub. See docs/content/cookbook/multilang.md.

# --- Python: nanobind extension + its bundled stubgen ---------------------------
nanobind_add_module(journal py_module.cpp)
target_link_libraries(journal PRIVATE welder::headers welder::nanobind)

nanobind_add_stub(journal_pyi
  MODULE journal
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/journal.pyi
  PYTHON_PATH $<TARGET_FILE_DIR:journal>
  DEPENDS journal)

add_test(NAME cookbook.07-multilang.python
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.07-multilang.python PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:journal>;JOURNAL_PYI=${CMAKE_CURRENT_BINARY_DIR}/journal.pyi")

# --- the LuaCATS ---@meta stub ---------------------------------------------------
# Pure reflection + text (welder::luacats — no Lua framework or runtime needed),
# styled to match the Lua bindings. journal.hpp mentions nanobind types in the
# Python-flavored method, so the generator TU needs those include paths.
welder_luacats_generate_stub(journal_luacats
  SOURCES stub_gen.cpp
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/journal-stub.lua
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/journal.hpp)
target_link_libraries(journal_luacats_gen PRIVATE welder::nanobind)
target_include_directories(journal_luacats_gen PRIVATE ${Python_INCLUDE_DIRS})

# --- Lua: one check script, up to two runtime rods -------------------------------
# Both modules load as `require("journal")` (luaopen_journal), so each target gets
# the module FILE name `journal.so` in its own subdirectory and the check runs
# once per rod with the matching LUA_CPATH. The Python target owns the `journal`
# TARGET name, hence the _sol2/_luabridge suffixes.
function(cookbook_add_lua_check rod_name module_target lua_interp)
  add_test(NAME cookbook.07-multilang.lua-${rod_name}
    COMMAND ${lua_interp} ${CMAKE_CURRENT_SOURCE_DIR}/check.lua)
  # \; keeps Lua's path separator out of CMake's list separator; the trailing ;;
  # appends Lua's default cpath.
  set_tests_properties(cookbook.07-multilang.lua-${rod_name} PROPERTIES ENVIRONMENT
    "LUA_CPATH=$<TARGET_FILE_DIR:${module_target}>/?.so\;\;;JOURNAL_LUACATS=${CMAKE_CURRENT_BINARY_DIR}/journal-stub.lua")
endfunction()

if(TARGET welder::sol2)
  welder_sol2_add_module(journal_sol2 lua_module.cpp)
  target_link_libraries(journal_sol2 PRIVATE welder::headers)
  # The mirror of the Python include story: the Python-flavored method mentions
  # nanobind types, so the Lua TUs need the nanobind/Python include paths (headers
  # only — nothing of Python is emitted into the Lua modules).
  target_link_libraries(journal_sol2 PRIVATE welder::nanobind)
  target_include_directories(journal_sol2 PRIVATE ${Python_INCLUDE_DIRS})
  set_target_properties(journal_sol2 PROPERTIES
    OUTPUT_NAME journal
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/sol2)

  if(WELDER_COOKBOOK_LUA)
    cookbook_add_lua_check(sol2 journal_sol2 ${WELDER_COOKBOOK_LUA})
  endif()
endif()

if(TARGET welder::luabridge)
  welder_luabridge_add_module(journal_luabridge lua_module_luabridge.cpp)
  target_link_libraries(journal_luabridge PRIVATE welder::headers welder::nanobind)
  target_include_directories(journal_luabridge PRIVATE ${Python_INCLUDE_DIRS})
  set_target_properties(journal_luabridge PROPERTIES
    OUTPUT_NAME journal
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/luabridge)

  if(WELDER_COOKBOOK_LUABRIDGE_LUA)
    cookbook_add_lua_check(luabridge journal_luabridge ${WELDER_COOKBOOK_LUABRIDGE_LUA})
  endif()
endif()