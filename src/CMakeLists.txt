# welder is header-only: a header-only core plus header-only language rods
# (backends). Everything lives under one tree (src/welder) so user includes always
# start from <welder/...>. (An optional C++20 module wrapper was removed until the
# gcc-16 -freflection/modules bugs are resolved — see docs "Header-only for now".)

# --- welder::headers : the header-only core (source of truth) ----------------
add_library(welder_headers INTERFACE)
add_library(welder::headers ALIAS welder_headers)
# src/ is the include root, so <welder/welder.hpp>, <welder/rods/python/pybind11/rod.hpp>
# and friends all resolve from here.
target_include_directories(welder_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>)
target_compile_features(welder_headers INTERFACE cxx_std_26)
target_link_libraries(welder_headers INTERFACE welder_flags)

# --- language rods (header-only adapters over the core resolution API) --------
add_subdirectory(welder/rods)
