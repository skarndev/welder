# Cookbook 07 — multilang: one annotated header shipped to Python (nanobind) and
# Lua (sol2), with a name style per language, a .pyi stub and a LuaCATS stub.
# See docs/content/cookbook/multilang.md.

# --- Python: nanobind extension + its bundled stubgen ---------------------------
# journal.hpp mentions sol2 types in the Lua-flavored method, so the Python TU
# needs the sol2/Lua INCLUDE PATHS too (welder::sol2 is include-only; nothing of
# Lua is linked or emitted into the Python module).
nanobind_add_module(journal py_module.cpp)
target_link_libraries(journal PRIVATE welder::headers welder::nanobind)
if(TARGET welder::sol2)
  target_link_libraries(journal PRIVATE welder::sol2)
endif()

nanobind_add_stub(journal_pyi
  MODULE journal
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/journal.pyi
  PYTHON_PATH $<TARGET_FILE_DIR:journal>
  DEPENDS journal)

add_test(NAME cookbook.07-multilang.python
  COMMAND Python::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check.py)
set_tests_properties(cookbook.07-multilang.python PROPERTIES
  ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:journal>;JOURNAL_PYI=${CMAKE_CURRENT_BINARY_DIR}/journal.pyi")

# --- Lua: sol2 loadable module (needs a real Lua install: WELDER_LUA_DIR) --------
if(TARGET welder::sol2)
  # The target can't also be called `journal` (the Python target owns that name),
  # so the module FILE name is set instead — `require("journal")` loads journal.so
  # and enters luaopen_journal.
  welder_sol2_add_module(journal_lua lua_module.cpp)
  target_link_libraries(journal_lua PRIVATE welder::headers)
  # ...and the mirror of the include story above: the Python-flavored method
  # mentions nanobind types, so the Lua TU needs the nanobind/Python include paths.
  target_link_libraries(journal_lua PRIVATE welder::nanobind)
  target_include_directories(journal_lua PRIVATE ${Python_INCLUDE_DIRS})
  set_target_properties(journal_lua PROPERTIES OUTPUT_NAME journal)

  # The LuaCATS ---@meta stub: pure reflection + text (welder::luacats), styled to
  # match the sol2 binding. The generator TU sees the same journal.hpp, so it also
  # needs the backend include paths.
  welder_luacats_generate_stub(journal_luacats
    SOURCES stub_gen.cpp
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/journal-stub.lua
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/journal.hpp)
  target_link_libraries(journal_luacats_gen PRIVATE welder::sol2 welder::nanobind)
  target_include_directories(journal_luacats_gen PRIVATE ${Python_INCLUDE_DIRS})

  if(WELDER_COOKBOOK_LUA)
    add_test(NAME cookbook.07-multilang.lua
      COMMAND ${WELDER_COOKBOOK_LUA} ${CMAKE_CURRENT_SOURCE_DIR}/check.lua)
    # \; keeps Lua's path separator out of CMake's list separator; the trailing
    # ;; appends Lua's default cpath.
    set_tests_properties(cookbook.07-multilang.lua PROPERTIES ENVIRONMENT
      "LUA_CPATH=$<TARGET_FILE_DIR:journal_lua>/?.so\;\;;JOURNAL_LUACATS=${CMAKE_CURRENT_BINARY_DIR}/journal-stub.lua")
  endif()
endif()