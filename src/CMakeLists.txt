# welder is header-only at its core, with an optional single importable module
# and header-only language backends. Everything lives under one tree (src/welder)
# so user includes always start from <welder/...>.

# --- welder::headers : the header-only core (source of truth) ----------------
add_library(welder_headers INTERFACE)
add_library(welder::headers ALIAS welder_headers)
# src/ is the include root, so <welder/welder.hpp>, <welder/backends/pybind11.hpp>
# and friends all resolve from here.
target_include_directories(welder_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>)
target_compile_features(welder_headers INTERFACE cxx_std_26)
target_link_libraries(welder_headers INTERFACE welder_flags)

# --- welder::module : optional `import welder;` wrapper (exports vocabulary) --
if(WELDER_BUILD_MODULE)
  add_library(welder_module STATIC)
  add_library(welder::module ALIAS welder_module)
  target_sources(welder_module
    PUBLIC
      FILE_SET welder_module_ifc TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/welder
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/welder/welder.cppm)
  target_compile_features(welder_module PUBLIC cxx_std_26)
  # PUBLIC so importers also get the include path (for backend headers) + flags.
  target_link_libraries(welder_module PUBLIC welder_headers welder_flags)
endif()

# --- language backends (header-only adapters over the core resolution API) ----
add_subdirectory(welder/backends)
