# welder_luabridge_add_module(<target> <source>...)
#
# Create a loadable Lua C extension from welder-generated LuaBridge3 bindings, the
# LuaBridge3 counterpart of welder_sol2_add_module(). The result is a shared object
# Lua's `require` can load: a bare `<target>.so` (Lua's default cpath has no `lib`
# prefix and expects `.so` even on macOS) exporting `luaopen_<target>`, with the
# `lua_*` symbols left to resolve from the host interpreter at load time — the module
# never bundles its own Lua runtime (see the welder::luabridge target for why). Link
# this target against welder::luabridge.
#
#   welder_luabridge_add_module(geometry_lua bindings.cpp)
#
# The <target> name must match the module name passed to
# WELDER_MODULE(ns, luabridge) / luaopen_<target>, since Lua derives the entry symbol
# from the require name.
function(welder_luabridge_add_module target)
  add_library(${target} MODULE ${ARGN})
  target_link_libraries(${target} PRIVATE welder::luabridge)
  # Bare name, `.so` suffix, on every platform Lua loads from cpath.
  set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".so")
  # Do NOT scan this TU for C++20 module imports. As with the sol2 rod, a Lua binding
  # TU is consumed header-only (`#include <welder/rods/lua/luabridge/rod.hpp>`, no
  # `import`), and p1689 module scanning turns system `#include`s into header units
  # whose macros don't leak (LuaBridge3 and <luaconf.h> both rely on macro
  # visibility), so scanning is both unneeded and harmful here.
  set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
  if(APPLE)
    # macOS: allow the unresolved lua_* symbols; the host interpreter supplies them
    # at dlopen. (On ELF a MODULE library already leaves them unresolved.)
    target_link_options(${target} PRIVATE -undefined dynamic_lookup)
  endif()
endfunction()