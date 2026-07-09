# welder_sol2_add_module(<target> <source>...)
#
# Create a loadable Lua C extension from welder-generated sol2 bindings, the way
# nanobind_add_module / PYBIND11_MODULE do for Python. The result is a shared
# object Lua's `require` can load: a bare `<target>.so` (Lua's default cpath has
# no `lib` prefix and expects `.so` even on macOS) exporting `luaopen_<target>`,
# with the `lua_*` symbols left to resolve from the host interpreter at load
# time — the module never bundles its own Lua runtime (see the welder::sol2
# target for why). Link this target against welder::sol2.
#
#   welder_sol2_add_module(geometry_lua bindings.cpp)
#
# The <target> name must match the module name passed to WELDER_MODULE(ns, sol2)
# / luaopen_<target>, since Lua derives the entry symbol from the require name.
function(welder_sol2_add_module target)
  add_library(${target} MODULE ${ARGN})
  target_link_libraries(${target} PRIVATE welder::sol2)
  # welder's own build applies its strict warning set; a consumer using this
  # helper won't have the target, so the link is skipped for them.
  if(TARGET welder_warnings)
    target_link_libraries(${target} PRIVATE welder_warnings)
  endif()
  # Bare name, `.so` suffix, on every platform Lua loads from cpath.
  set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".so")
  # Do NOT scan this TU for C++20 module imports. When the project builds the
  # `welder` module (FILE_SET CXX_MODULES), CMake turns on p1689 module scanning
  # for every C++ source; under `-fmodules-ts` a system `#include` becomes a header
  # unit whose *macros* don't leak, so sol2's <luaconf.h> can't see LLONG_MAX and
  # fails its `long long` check. A sol2 binding TU uses no `import` (consume welder
  # header-only, `#include <welder/welder.hpp>`), so scanning is unneeded here.
  set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
  if(APPLE)
    # macOS: allow the unresolved lua_* symbols; the host interpreter supplies
    # them at dlopen. (On ELF a MODULE library already leaves them unresolved.)
    target_link_options(${target} PRIVATE -undefined dynamic_lookup)
  endif()
endfunction()
